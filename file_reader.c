#include "file_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#define BLOCK_SIZE 512

struct disk_t *disk_open_from_file(const char *volume_file_name) {
    if (volume_file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }

    FILE *file = fopen(volume_file_name, "rb");
    if (file == NULL) {
        errno = ENOENT;
        return NULL;
    }

    struct disk_t *pdisk = malloc(sizeof(struct disk_t));
    if (pdisk == NULL) {
        fclose(file);
        errno = ENOMEM;
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    pdisk->block_count = ftell(file) / BLOCK_SIZE;
    fseek(file, 0, SEEK_SET);
    pdisk->file = file;

    return pdisk;
}

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read) {
    if (pdisk == NULL || pdisk->file == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (first_sector < 0 || first_sector + sectors_to_read > pdisk->block_count) {
        errno = ERANGE;
        return -1;
    }

    fseek(pdisk->file, first_sector * BLOCK_SIZE, SEEK_SET);
    fread(buffer, BLOCK_SIZE, sectors_to_read, pdisk->file);
    return sectors_to_read;
}

int disk_close(struct disk_t *pdisk) {
    if (pdisk == NULL || pdisk->file == NULL) {
        errno = EFAULT;
        return -1;
    }

    fclose(pdisk->file);
    free(pdisk);
    return 0;
}


struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector) {
    if (pdisk == NULL) {
        errno = EFAULT;
        return NULL;
    }

    struct volume_t *pvolume = malloc(sizeof(struct volume_t));
    if (pvolume == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (disk_read(pdisk, first_sector, pvolume, 1) < 0) {
        free(pvolume);
        return NULL;
    }

    if (pvolume->signature != 0xaa55) {
        free(pvolume);
        errno = EINVAL;
        return NULL;
    }

    pvolume->pdisk = pdisk;

    return pvolume;
}

int fat_close(struct volume_t *pvolume) {
    if (pvolume == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(pvolume);
    return 0;
}

struct clusters_chain_t *get_chain_fat12(const void *const buffer, size_t size, uint16_t first_cluster) {
    const uint8_t *const fat_buffer = (const uint8_t *const) buffer;
    if (fat_buffer == NULL || size < 3) {
        return NULL;
    }

    //printf("%d %d %d", fat_buffer[0], fat_buffer[1], fat_buffer[2]);
    uint16_t terminator = fat_buffer[0] + (fat_buffer[1] & 0x0F) * 256;
    printf("%d\n", terminator);
    //uint16_t terminator = 0xFFB;
    uint16_t current_cluster = first_cluster;
    uint16_t next_cluster;

    struct clusters_chain_t *output = malloc(sizeof(struct clusters_chain_t));
    if (output == NULL) {
        return NULL;
    }
    output->clusters = NULL;
    output->size = 0;

    while (current_cluster != terminator) {

        //printf("%d %d %d", fat_buffer[0], fat_buffer[1], fat_buffer[2]);
        //printf("%d\n", current_cluster);
        output->size++;
        uint16_t* temp = (uint16_t *) realloc(output->clusters, output->size * sizeof(uint16_t));
        if (temp == NULL) {
            free(output->clusters);
            free(output);
            return NULL;
        }
        output->clusters = temp;
        output->clusters[output->size - 1] = current_cluster;
        if (current_cluster % 2) {
            next_cluster = (fat_buffer[current_cluster + current_cluster / 2] >> 4) +
                           fat_buffer[current_cluster + current_cluster / 2 + 1] * 16;
        } else {
            next_cluster = fat_buffer[current_cluster + current_cluster / 2] +
                           (fat_buffer[current_cluster + current_cluster / 2 + 1] & 0x0F) * 256;
        }
        current_cluster = next_cluster;
    }

    return output;
}

struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {
    if (pvolume == NULL || file_name == NULL) {
        //printf("EFAULT\n");
        errno = EFAULT;
        return NULL;
    }

    struct file_t *pfile = malloc(sizeof(struct file_t));
    if (pvolume == NULL) {
        //printf("ENOMEM\n");
        errno = ENOMEM;
        return NULL;
    }

    struct correct_dir_entry_t *root_dir = malloc(pvolume->root_dir_capacity * 32);

    if (disk_read(pvolume->pdisk, pvolume->fats_count * pvolume->size_of_fat + pvolume->size_of_reserved_area, root_dir,
                  pvolume->root_dir_capacity * sizeof(struct correct_dir_entry_t) / pvolume->bytes_per_sector) < 0) {
        //printf("disk_read\n");
        free(pfile);
        return NULL;
    }

    char *dot = strchr(file_name, '.');
    for (int i = 0; i < pvolume->root_dir_capacity; i++) {
        printf("%s\n", (root_dir + i)->name);
        //printf("%d", strncmp((root_dir + i)->name, file_name, strlen(file_name) - strlen(dot)));
        if ((dot == NULL && strncmp((root_dir + i)->name, file_name, strlen(file_name)) == 0) ||
            (dot != NULL && strncmp((root_dir + i)->name, file_name, strlen(file_name) - strlen(dot)) == 0 &&
             strncmp((root_dir + i)->name + 8, dot + 1, strlen(dot + 1)) == 0)) {
            if ((root_dir + i)->file_attributes & 0x18) {
                free(root_dir);
                free(pfile);
                //printf("EISDIR\n");
                errno = EISDIR;
                return NULL;
            }

            pfile->entry = malloc(sizeof(struct correct_dir_entry_t));
            if (pvolume == NULL) {
                //printf("ENOMEM\n");
                errno = ENOMEM;
                return NULL;
            }
            memcpy(pfile->entry, root_dir + i, sizeof(struct correct_dir_entry_t));
            free(root_dir);

            pfile->whence = 0;

            uint8_t *FATs = malloc(pvolume->fats_count * pvolume->size_of_fat * pvolume->bytes_per_sector);
            if (disk_read(pvolume->pdisk, pvolume->size_of_reserved_area, FATs,
                          pvolume->fats_count * pvolume->size_of_fat) < 0) {
                //printf("disk_read\n");
                free(pfile);
                return NULL;
            }
            pfile->pvolume = pvolume;
            pfile->clusters_chain = get_chain_fat12(FATs, pvolume->fats_count * pvolume->size_of_fat,
                                                    pfile->entry->low_order_address_of_first_cluster);

            free(FATs);
            return pfile;
        }
    }

//printf("ENOENT\n");
    free(root_dir);
    free(pfile);
    errno = ENOENT;
    return NULL;
}

int file_close(struct file_t *stream) {
    if (stream == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(stream->entry);
    free(stream->clusters_chain->clusters);
    free(stream->clusters_chain);
    free(stream);
    return 0;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (ptr == NULL || stream == NULL) {
        errno = EFAULT;
        return -1;
    }
    int bytes_per_cluster = stream->pvolume->sectors_per_cluster * stream->pvolume->bytes_per_sector;
    size_t bytes_left = nmemb * size;
    size_t position = 0;
    size_t i = stream->whence / bytes_per_cluster;
    size_t bytes_left_to_skip = stream->whence % bytes_per_cluster;
    /*if(i >= stream->clusters_chain->size || bytes_left == 0 || stream->entry->file_size <= stream->whence){
        printf("EOF");
        errno = ERANGE;
        return -1;
    }*/
    for (; i < stream->clusters_chain->size && bytes_left > 0 && stream->entry->file_size > stream->whence; i++) {
        int32_t first_byte = (int32_t)(((stream->pvolume->fats_count * stream->pvolume->size_of_fat +
                                 stream->pvolume->size_of_reserved_area) * stream->pvolume->bytes_per_sector) +
                                       (bytes_per_cluster * (stream->clusters_chain->clusters[i] - 2)) +
                                       (stream->pvolume->root_dir_capacity * sizeof(struct correct_dir_entry_t)) + bytes_left_to_skip);
        int32_t bytes_to_read = bytes_per_cluster - (int32_t)bytes_left_to_skip;
        bytes_to_read = bytes_to_read < (int32_t)bytes_left ? bytes_to_read : (int32_t)bytes_left;
        bytes_to_read = bytes_to_read < (int32_t)(stream->entry->file_size - stream->whence) ? bytes_to_read : (int32_t)(stream->entry->file_size - stream->whence);

        if (first_byte < 0 || bytes_to_read < 0 || first_byte + bytes_to_read > stream->pvolume->pdisk->block_count * stream->pvolume->bytes_per_sector) {
            printf("EOF");
            errno = ERANGE;
            return -1;
        }

        fseek(stream->pvolume->pdisk->file, first_byte, SEEK_SET);
        fread((char *)ptr + position, bytes_to_read, 1, stream->pvolume->pdisk->file);

        stream->whence += bytes_to_read;
        position += bytes_to_read;
        bytes_left -= (size_t)bytes_to_read > bytes_left ? bytes_left : (size_t)bytes_to_read;
        bytes_left_to_skip = 0;
    }
    //printf("%s", (char*)ptr);
    return position / size;
}

int32_t file_seek(struct file_t *stream, int32_t offset, int whence) {
    if(stream == NULL){
        errno = EFAULT;
        return -1;
    }

    size_t newWhence;

    switch(whence){
        case SEEK_SET:
            newWhence = offset;
            break;
        case SEEK_CUR:
            newWhence = stream->whence + offset;
            break;
        case SEEK_END:
            newWhence = stream->entry->file_size + offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if(newWhence > stream->entry->file_size){
        errno = ENXIO;
        return -1;
    }

    stream->whence = newWhence;
    return newWhence;
}

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path) {
    if (pvolume == NULL || dir_path == NULL) {
        //printf("EFAULT\n");
        errno = EFAULT;
        return NULL;
    }

    if(strcmp(dir_path, "\\")){
        errno = ENOENT;
        return NULL;
    }

    struct dir_t *pdir = malloc(sizeof(struct dir_t));
    if (pvolume == NULL) {
        //printf("ENOMEM\n");
        errno = ENOMEM;
        return NULL;
    }

    pdir->pvolume = pvolume;
    pdir->whence = 0;
    pdir->dir_entries = malloc(pvolume->root_dir_capacity * 32);

    if (disk_read(pvolume->pdisk, pvolume->fats_count * pvolume->size_of_fat + pvolume->size_of_reserved_area, pdir->dir_entries,
                  pvolume->root_dir_capacity * sizeof(struct correct_dir_entry_t) / pvolume->bytes_per_sector) < 0) {
        //printf("disk_read\n");
        free(pdir);
        return NULL;
    }

    return pdir;
}

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry) {
    if (pdir == NULL || pentry == NULL) {
        //printf("EFAULT\n");
        errno = EFAULT;
        return -1;
    }

    if(pdir->whence >= pdir->pvolume->root_dir_capacity){
        return 1;
    }

    while(!isalpha(pdir->dir_entries[pdir->whence].name[0])){
        pdir->whence++;
        if(pdir->whence >= pdir->pvolume->root_dir_capacity){
            return 1;
        }
    }

    pentry->size = pdir->dir_entries[pdir->whence].file_size;
    pentry->is_archived = pdir->dir_entries[pdir->whence].file_attributes & 0x20;
    pentry->is_readonly = pdir->dir_entries[pdir->whence].file_attributes & 0x01;
    pentry->is_system = pdir->dir_entries[pdir->whence].file_attributes & 0x04;
    pentry->is_hidden = pdir->dir_entries[pdir->whence].file_attributes & 0x02;
    pentry->is_directory = pdir->dir_entries[pdir->whence].file_attributes & 0x10;

    int i = 0;
    int j = 8;
    while(isalpha(pdir->dir_entries[pdir->whence].name[i]) && i < 8){
        pentry->name[i] = pdir->dir_entries[pdir->whence].name[i];
        i++;
    }

    if(!isalpha(pdir->dir_entries[pdir->whence].name[j])){
        pentry->name[i] = '\0';
        pdir->whence++;
        return 0;
    }

    pentry->name[i++] = '.';
    while(isalpha(pdir->dir_entries[pdir->whence].name[j]) && j < 11){
        pentry->name[i] = pdir->dir_entries[pdir->whence].name[j];
        j++;
        i++;
    }

    pentry->name[i] = '\0';
    pdir->whence++;
    return 0;
}

int dir_close(struct dir_t *pdir) {
    if (pdir == NULL) {
        errno = EFAULT;
        return -1;
    }

    free(pdir->dir_entries);
    free(pdir);
    return 0;
}
