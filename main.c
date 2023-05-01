#include <stdio.h>
#include <malloc.h>
#include "file_reader.h"

int main(void) {
    struct disk_t *disk = disk_open_from_file("0001round_fat12_volume.img");
    if (disk == NULL)
    {
        printf("Wrong\n");
    }
    else
    {
        printf("Good\n");
    }
    struct volume_t *fat = fat_open(disk, 0);
    if (fat == NULL)
    {
        printf("Wrong\n");
    }
    else
    {
        printf("Good\n");
    }
    printf("%d\n", fat->bytes_per_sector);
    struct file_t *file = file_open(fat, "ENOUGH.TX");
    if (file == NULL)
    {
        printf("Wrong\n");
    }
    else
    {
        printf("Good\n");
    }
    /*char *text = malloc(file->pvolume->sectors_per_cluster * file->pvolume->bytes_per_sector * file->clusters_chain->size);
    printf("%d\n", file->pvolume->sectors_per_cluster * file->pvolume->bytes_per_sector);
    for (int i = 0; i < 512; ++i)
    {
        char c;
        file_read(&c, 1, 1, file);
        printf("%d %c\n", i, c);
    }
    for (int i = 512; i < 3565; ++i)
    {
        char c;
        file_read(&c, 1, 1, file);
        printf("%d %c\n", i, c);
    }
    free(text);*/
    fat_close(fat);
    disk_close(disk);
    return 0;
}


