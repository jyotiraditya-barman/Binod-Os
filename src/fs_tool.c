#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define MAGIC 0xDEADBEEF
#define DIR_COUNT 32

struct superblock {
    uint32_t magic;
    uint32_t dir_offset;
    uint32_t dir_size;
    uint32_t data_offset;
};

struct dir_entry {
    char name[16];
    uint32_t offset;
    uint32_t size;
    uint8_t used;
};

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s disk.img input.txt filename_in_fs\n", argv[0]);
        return 1;
    }

    FILE *img = fopen(argv[1], "rb+");
    if (!img) { printf("Cannot open disk image\n"); return 1; }

    FILE *in = fopen(argv[2], "rb");
    if (!in) { printf("Cannot open input file\n"); return 1; }

    struct superblock sb;
    fread(&sb, sizeof(sb), 1, img);


    fseek(img, sb.dir_offset, SEEK_SET);

    struct dir_entry dir[DIR_COUNT];
    fread(dir, sizeof(dir), 1, img);

    // Find empty slot
    int slot = -1;
    for (int i = 0; i < DIR_COUNT; i++) {
        if (!dir[i].used) { slot = i; break; }
    }
    if (slot == -1) { printf("Directory full\n"); return 1; }

    // Prepare new entry
    struct dir_entry *e = &dir[slot];
    strncpy(e->name, argv[3], 16);
    e->used = 1;

    // Put file at end of FS
    fseek(img, 0, SEEK_END);
    e->offset = ftell(img);

    // Copy file data
    uint8_t buffer[4096];
    int total = 0, n;

    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        fwrite(buffer, 1, n, img);
        total += n;
    }

    e->size = total;

    // Write directory back
    fseek(img, sb.dir_offset, SEEK_SET);
    fwrite(dir, sizeof(dir), 1, img);

    printf("Inserted file '%s' (%d bytes)\n", argv[3], total);
    return 0;
}
