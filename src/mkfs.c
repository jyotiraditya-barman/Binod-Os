/* mkfs.c - host tool to create raw disk image and format the tiny FS */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define IMG "disk.img"
#define SIZE_MB 10
#define SECTOR 512
#define TOTAL_SECTORS ((SIZE_MB*1024*1024)/SECTOR)

#define FS_SUPER_LBA    1
#define FS_BITMAP_LBA   2
#define FS_BITMAP_SECTS 16
#define FS_ROOT_LBA     (FS_BITMAP_LBA + FS_BITMAP_SECTS)
#define FS_ROOT_SECTS   8
#define FS_DATA_LBA     (FS_ROOT_LBA + FS_ROOT_SECTS)
#define FS_MAGIC 0x42494E4F

int write_sector(FILE *f, uint32_t lba, const void *buf) {
    if (fseek(f, lba * SECTOR, SEEK_SET)) return -1;
    if (fwrite(buf, SECTOR, 1, f) != 1) return -1;
    return 0;
}

int main() {
    FILE *f = fopen(IMG, "wb+");
    if (!f) { perror("open"); return 1; }
    // allocate file of SIZE_MB MB
    if (fseek(f, SIZE_MB*1024*1024 - 1, SEEK_SET) != 0) { perror("seek"); return 1; }
    if (fwrite("\0", 1, 1, f) != 1) { perror("write"); return 1; }

    // write superblock
    uint8_t buf[512];
    memset(buf,0,512);
    uint32_t magic = FS_MAGIC;
    memcpy(buf, &magic, 4);
    uint32_t version = 1; memcpy(buf+4, &version, 4);
    uint32_t total = TOTAL_SECTORS; memcpy(buf+8, &total, 4);
    uint32_t data_lba = FS_DATA_LBA; memcpy(buf+12, &data_lba, 4);
    write_sector(f, FS_SUPER_LBA, buf);

    // clear bitmap
    memset(buf,0x00,512);
    for (int i=0;i<FS_BITMAP_SECTS;i++) write_sector(f, FS_BITMAP_LBA + i, buf);
return 0;
    // mark bitmap for FS metadata sectors as used (super, bitmap, root)
}