#ifndef FAT_FILE_READER_H
#define FAT_FILE_READER_H

#include <stdint-gcc.h>
#include <stddef.h>
#include <dirent.h>
#include <bits/types/FILE.h>

struct disk_t {
    FILE *file;
    int32_t block_count;
};

struct volume_t {
    uint8_t jump_code[3];
    char oem_name[8];

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t size_of_reserved_area;
    uint8_t fats_count;
    uint16_t root_dir_capacity;
    uint16_t sectors_count;
    uint8_t media_type;
    uint16_t size_of_fat;
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t sectors_before_partition_count;
    uint32_t sectors_in_filesystem_count;

    uint8_t drive_number;
    uint8_t not_used_1;
    uint8_t boot_signature;
    uint32_t volume_serial_number;
    char volume_label[11];
    char type_level[8];
    uint8_t not_used_2[448];
    uint16_t signature;

    struct disk_t *pdisk;
} __attribute__(( packed ));

struct clusters_chain_t{
    uint16_t *clusters;
    size_t size;
};

struct file_t {
    struct volume_t *pvolume;
    struct correct_dir_entry_t* entry;
    size_t whence;
    struct clusters_chain_t* clusters_chain;
};

struct correct_dir_entry_t {
    //uint8_t allocation_status;
    char name[11]; //10
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t file_size;
} __attribute__(( packed ));

struct dir_entry_t {
    char name[12];
    uint8_t is_archived;
    uint8_t is_readonly;
    uint8_t is_system;
    uint8_t is_hidden;
    uint8_t is_directory;
    uint32_t size;
} __attribute__(( packed ));

struct dir_t {
    struct correct_dir_entry_t* dir_entries;
    struct volume_t *pvolume;
    size_t whence;
};

struct disk_t *disk_open_from_file(const char *volume_file_name);

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read);

int disk_close(struct disk_t *pdisk);

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector);

int fat_close(struct volume_t *pvolume);

struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster);

struct file_t *file_open(struct volume_t *pvolume, const char *file_name);

int file_close(struct file_t *stream);

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);

int32_t file_seek(struct file_t *stream, int32_t offset, int whence);

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path);

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry);

int dir_close(struct dir_t *pdir);

#endif //FAT_FILE_READER_H
