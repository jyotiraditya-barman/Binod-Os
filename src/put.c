#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define SECTOR 512
#define FS_SUPER_LBA    1
#define FS_BITMAP_LBA   2
#define FS_BITMAP_SECTS 16
#define FS_ROOT_LBA     (FS_BITMAP_LBA + FS_BITMAP_SECTS)
#define FS_ROOT_SECTS   8
#define FS_DATA_LBA     (FS_ROOT_LBA + FS_ROOT_SECTS)

struct DirEntry {
    char name[16];
    uint32_t size;
    uint32_t start;
    uint32_t blocks;
};

uint8_t bitmap[FS_BITMAP_SECTS * 512];

static void set_bitmap(uint32_t block)
{
    bitmap[block / 8] |= (1 << (block % 8));
}

static int find_free_blocks(int need, uint32_t *start)
{
    int count = 0;
    for (uint32_t i = FS_DATA_LBA; i < 100000; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if (!(bitmap[byte] & (1 << bit))) {
            if (count == 0) *start = i;
            count++;
            if (count == need) return 1;
        } else count = 0;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: put disk.img file\n");
        return 1;
    }

    FILE *img = fopen(argv[1], "rb+");
    if (!img) { perror("open img"); return 1; }

    FILE *in = fopen(argv[2], "rb");
    if (!in) { perror("open input"); return 1; }

    // Read bitmap
    fseek(img, FS_BITMAP_LBA*SECTOR, SEEK_SET);
    fread(bitmap, 1, FS_BITMAP_SECTS*SECTOR, img);

    // Load directory
    struct DirEntry dir[128];
    fseek(img, FS_ROOT_LBA*SECTOR, SEEK_SET);
    fread(dir, sizeof(dir), 1, img);

    // Find free directory entry
    int d = -1;
    for (int i = 0; i < 128; i++) {
        if (dir[i].name[0] == 0) { d = i; break; }
    }
    if (d < 0) { printf("No directory entries left\n"); return 1; }

    // Read file
    fseek(in, 0, SEEK_END);
    uint32_t size = ftell(in);
    fseek(in, 0, SEEK_SET);

    uint32_t blocks = (size + 511) / 512;
    uint32_t start;

    if (!find_free_blocks(blocks, &start)) {
        printf("Not enough space.\n");
        return 1;
    }

    // Mark blocks in bitmap
    for (uint32_t i = 0; i < blocks; i++)
        set_bitmap(start + i);

    // Write bitmap back
    fseek(img, FS_BITMAP_LBA*512, SEEK_SET);
    fwrite(bitmap, 1, FS_BITMAP_SECTS*SECTOR, img);

    // Write data blocks
    uint8_t buf[512];
    for (uint32_t i = 0; i < blocks; i++) {
        memset(buf, 0, 512);
        fread(buf, 1, 512, in);

        fseek(img, (start + i) * 512, SEEK_SET);
        fwrite(buf, 1, 512, img);
    }

    // Write directory entry
    strncpy(dir[d].name, argv[2], 15);
    dir[d].size = size;
    dir[d].start = start;
    dir[d].blocks = blocks;

    fseek(img, FS_ROOT_LBA*512, SEEK_SET);
    fwrite(dir, sizeof(dir), 1, img);

    printf("Added file '%s' (%u bytes) at LBA %u\n", argv[2], size, start);
    return 0;
}
