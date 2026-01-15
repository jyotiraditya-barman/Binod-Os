/* fs.c - tiny persistent filesystem using ATA PIO
   Updated to avoid <string.h> by providing small local helpers.
*/

#include "fs.h"
#include "ata.h"
#include <stdint.h>
#include "io.h"
/* ------------------ small kernel-safe helpers ------------------ */
/* We implement tiny versions of memcpy/memset/strlen/strncpy/strncmp
   as static so they don't conflict with external libraries. */

static void *memcpy_small(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *memset_small(void *dst, int value, uint32_t n) {
    uint8_t *d = (uint8_t*)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)value;
    return dst;
}

static uint32_t strlen_small(const char *s) {
    uint32_t c = 0;
    while (s && s[c]) c++;
    return c;
}

/* strncpy_small: copies at most n-1 bytes then NUL-terminates (safe for kernel use) */
static char *strncpy_small(char *dst, const char *src, uint32_t n) {
    if (n == 0) return dst;
    uint32_t i = 0;
    for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

/* strncmp_small: compare up to n chars */
static int strncmp_small(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc) return ac - bc;
        if (ac == 0) return 0;
    }
    return 0;
}

/* ---------- Layout/constants (same as before) ---------- */
#define FS_SUPER_LBA    1
#define FS_BITMAP_LBA   2
#define FS_BITMAP_SECTS 16
#define FS_ROOT_LBA     (FS_BITMAP_LBA + FS_BITMAP_SECTS)
#define FS_ROOT_SECTS   8
#define FS_DATA_LBA     (FS_ROOT_LBA + FS_ROOT_SECTS)

#define FS_MAX_FILES    128
#define FS_FILENAME_MAX 32
#define FS_BLOCK_SIZE   512

/* superblock structure (stored in LBA 1) */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t total_sectors;
    uint32_t data_lba;
    uint8_t  reserved[512 - 16];
} __attribute__((packed)) fs_super_t;

/* directory entry */
typedef struct {
    char name[FS_FILENAME_MAX];
    uint32_t start_block; /* LBA of first block */
    uint32_t size;        /* in bytes */
    uint8_t used;
    uint8_t pad[3];
} __attribute__((packed)) fs_dirent_t;

/* in-memory cache sectors */
static uint8_t sector_buf[512];
static fs_super_t superblock;
static int fs_ready = 0;

/* helpers to call ATA */
static int read_sector(uint32_t lba, void *buf) {
    return ata_read_sector(lba, (uint8_t*)buf);
}
static int write_sector(uint32_t lba, const void *buf) {
    return ata_write_sector(lba, (const uint8_t*)buf);
}

/* load superblock; if invalid, return -1 */
int fs_init(void) {
    if (read_sector(FS_SUPER_LBA, sector_buf) != 0) return -1;
    memcpy_small(&superblock, sector_buf, sizeof(fs_super_t));
    if (superblock.magic != FS_MAGIC) {
        fs_ready = 0;
        return -1;
    }
    fs_ready = 1;
    return 0;
}

/* internal: find dir entry index, or -1 if not found */
static int dir_find(const char *name, fs_dirent_t *out) {
    uint32_t lba = FS_ROOT_LBA;
    for (int s = 0; s < FS_ROOT_SECTS; s++) {
        if (read_sector(lba + s, sector_buf) != 0) return -1;
        fs_dirent_t *ents = (fs_dirent_t*)sector_buf;
        int entries = 512 / sizeof(fs_dirent_t);
        for (int i = 0; i < entries; i++) {
            if (ents[i].used) {
                if (strncmp_small(ents[i].name, name, FS_FILENAME_MAX) == 0) {
                    if (out) memcpy_small(out, &ents[i], sizeof(fs_dirent_t));
                    return (s * entries + i);
                }
            }
        }
    }
    return -1;
}

/* list directory */
int fs_list(void) {
    if (!fs_ready) return -1;
    uint32_t lba = FS_ROOT_LBA;
    /* print header once */
    printf_k("filename\t|\tsize\n");
    for (int s = 0; s < FS_ROOT_SECTS; s++) {
        if (read_sector(lba + s, sector_buf) != 0) return -1;
        fs_dirent_t *ents = (fs_dirent_t*)sector_buf;
        int entries = 512 / sizeof(fs_dirent_t);
        for (int i = 0; i < entries; i++) {
            if (ents[i].used) {
                printf_col("%s\t|\t%u bytes\n", ents[i].name, ents[i].size);
            }
        }
    }
    return 0;
}

/* helper: find free dir slot and return its global index; -1 if none */
/* helper: find free dir slot and return its sector LBA and index; -1 if none */
static int dir_find_free_slot(uint32_t *out_lba_sector, int *out_index) {
    uint32_t lba = FS_ROOT_LBA;
    for (int s = 0; s < FS_ROOT_SECTS; s++) {
        if (read_sector(lba + s, sector_buf) != 0) return -1;
        fs_dirent_t *ents = (fs_dirent_t*)sector_buf;
        int entries = 512 / sizeof(fs_dirent_t);
        for (int i = 0; i < entries; i++) {
            if (!ents[i].used) {
                if (out_lba_sector) *out_lba_sector = lba + s;
                if (out_index) *out_index = i;
                return 0;
            }
        }
    }
    return -1;
}

/* find free block by scanning bitmap; return LBA of free block, or 0 on error */
/* find a contiguous run of free blocks of length 'needed' and return starting LBA, 0 on failure */
static uint32_t bitmap_find_range(uint32_t needed) {
    if (needed == 0) return 0;
    /* scan bitmap bit-by-bit for a contiguous run */
    uint32_t total_bits = FS_BITMAP_SECTS * 512 * 8;
    uint32_t run = 0;
    uint32_t start_bit = 0;
    /* read sectors as needed */
    for (uint32_t bit_index = 0; bit_index < total_bits; bit_index++) {
        uint32_t byte_index = bit_index / 8;
        uint32_t sector = FS_BITMAP_LBA + (byte_index / 512);
        uint32_t offset = byte_index % 512;
        if (read_sector(sector, sector_buf) != 0) return 0;
        uint8_t b = sector_buf[offset];
        uint8_t bit = 1 << (bit_index % 8);
        int is_used = (b & bit) != 0;
        if (!is_used) {
            if (run == 0) start_bit = bit_index;
            run++;
            if (run >= needed) {
                /* convert start_bit to LBA */
                uint32_t lba = FS_DATA_LBA + start_bit;
                return lba;
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

/* set/clear a bit in bitmap */
static int bitmap_set_block(uint32_t block_lba, int value) {
    if (block_lba < FS_DATA_LBA) return -1;
    uint32_t bit_index = block_lba - FS_DATA_LBA;
    uint32_t byte_index = bit_index / 8;
    uint32_t bit = bit_index % 8;
    uint32_t sector = FS_BITMAP_LBA + (byte_index / 512);
    uint32_t offset = byte_index % 512;
    if (read_sector(sector, sector_buf) != 0) return -1;
    if (value)
        sector_buf[offset] |= (1 << bit);
    else
        sector_buf[offset] &= ~(1 << bit);
    if (write_sector(sector, sector_buf) != 0) return -1;
    return 0;
}

/* helper write file data into blocks (simple: allocate continuous blocks) */
static int write_data_contiguous(uint32_t start_lba, const uint8_t *data, uint32_t size) {
    uint32_t blocks = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    for (uint32_t b = 0; b < blocks; b++) {
        uint8_t tmp[512];
        uint32_t copy = (size > (b+1)*FS_BLOCK_SIZE) ? FS_BLOCK_SIZE : (size - b*FS_BLOCK_SIZE);
        if (copy > 0) {
            memcpy_small(tmp, data + b * FS_BLOCK_SIZE, copy);
        } else {
            /* nothing to copy - zero the block */
        }
        if (copy < FS_BLOCK_SIZE) memset_small(tmp + copy, 0, FS_BLOCK_SIZE - copy);
        if (write_sector(start_lba + b, tmp) != 0) return -1;
    }
    return 0;
}

/* create or overwrite a file */
int fs_write_file(const char *name, const void *data, int size) {
    if (!fs_ready) return -1;
    if (!name || name[0] == 0) return -1;
    if (strlen_small(name) >= FS_FILENAME_MAX) return -1;

    /* check existing */
    fs_dirent_t existing;
    int idx = dir_find(name, &existing);
    /* if existing found, we'll update its directory entry later; do not free its blocks yet
       - freeing is done only after a successful relocation to avoid data loss on failures */
    (void)existing;

    uint32_t needed = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;

    /* attempt to reuse existing region if it fits and is still free/owned */
    uint32_t new_start = 0;
    if (idx >= 0) {
        uint32_t old_blocks = (existing.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
        if (old_blocks >= needed) {
            /* reuse same start if it has enough blocks (we keep them allocated) */
            new_start = existing.start_block;
        }
    }

    if (new_start == 0) {
        /* find a contiguous run */
        new_start = bitmap_find_range(needed);
        if (new_start == 0) return -1;
        /* mark new blocks allocated */
        for (uint32_t k = 0; k < needed; k++) {
            if (bitmap_set_block(new_start + k, 1) != 0) {
                /* on failure, roll back previous allocations */
                for (uint32_t j = 0; j < k; j++) bitmap_set_block(new_start + j, 0);
                return -1;
            }
        }
    }

    /* write data */
    if (write_data_contiguous(new_start, (const uint8_t*)data, size) != 0) {
        /* on failure, if we allocated new blocks, free them */
        if (new_start != existing.start_block) {
            uint32_t allocated = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
            for (uint32_t k = 0; k < allocated; k++) bitmap_set_block(new_start + k, 0);
        }
        return -1;
    }

    /* write or update dir entry */
    uint32_t entries_per_sector = 512 / sizeof(fs_dirent_t);
    if (idx >= 0) {
        uint32_t sector = FS_ROOT_LBA + (idx / entries_per_sector);
        if (read_sector(sector, sector_buf) != 0) return -1;
        fs_dirent_t *ents = (fs_dirent_t*)sector_buf;
        int local_i = idx % entries_per_sector;
        strncpy_small(ents[local_i].name, name, FS_FILENAME_MAX);
        ents[local_i].start_block = new_start;
        ents[local_i].size = size;
        ents[local_i].used = 1;
        if (write_sector(sector, sector_buf) != 0) return -1;
        /* free old blocks if we moved */
        if (existing.start_block != 0 && existing.start_block != new_start) {
            uint32_t old_blocks = (existing.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
            for (uint32_t b = 0; b < old_blocks; b++) bitmap_set_block(existing.start_block + b, 0);
        }
    } else {
        uint32_t dir_lba;
        int dir_index;
        if (dir_find_free_slot(&dir_lba, &dir_index) != 0) return -1;
        if (read_sector(dir_lba, sector_buf) != 0) return -1;
        fs_dirent_t *ents = (fs_dirent_t*)sector_buf;
        strncpy_small(ents[dir_index].name, name, FS_FILENAME_MAX);
        ents[dir_index].start_block = new_start;
        ents[dir_index].size = size;
        ents[dir_index].used = 1;
        if (write_sector(dir_lba, sector_buf) != 0) return -1;
    }
    return 0;
}

/* run a binary file by loading it into an execution buffer and calling it.
   This assumes binaries are position-independent flat code suitable for direct call. */
/* Minimal ELF loader: supports ELF32 PT_LOAD segments by sliding into a run buffer.
   This is NOT a full ELF loader (no relocations, no dynamic linking). It expects
   relatively small, position-independent or relocatable test programs.
*/
int fs_run(const char *name) {
    if (!fs_ready) return -1;
    if (!name || name[0] == 0) return -1;
    fs_dirent_t d;
    if (dir_find(name, &d) < 0) return -1;
    if (d.size == 0) return -1;

    enum { RUN_BUF_SIZE = 65536 };
    static uint8_t file_buf[RUN_BUF_SIZE];
    static uint8_t run_buf[RUN_BUF_SIZE];
    if ((uint32_t)d.size > RUN_BUF_SIZE) return -1;
    int r = fs_read_file(name, file_buf, RUN_BUF_SIZE);
    if (r < 0) return -1;

    /* ELF32 structures */
    typedef struct {
        unsigned char e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint32_t e_entry;
        uint32_t e_phoff;
        uint32_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    } __attribute__((packed)) Elf32_Ehdr;

    typedef struct {
        uint32_t p_type;
        uint32_t p_offset;
        uint32_t p_vaddr;
        uint32_t p_paddr;
        uint32_t p_filesz;
        uint32_t p_memsz;
        uint32_t p_flags;
        uint32_t p_align;
    } __attribute__((packed)) Elf32_Phdr;

    if ((uint32_t)r < sizeof(Elf32_Ehdr)) return -1;
    Elf32_Ehdr *eh = (Elf32_Ehdr*)file_buf;
    /* check ELF magic */
    if (!(eh->e_ident[0] == 0x7f && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F')) {
        /* not ELF - treat as flat binary: copy into run_buf and call */
        memcpy_small(run_buf, file_buf, r);
        void (*entry_flat)(void) = (void(*)(void))run_buf;
        entry_flat();
        return 0;
    }
    /* only support 32-bit little-endian here */
    if (eh->e_ident[4] != 1) return -1; /* ELFCLASS32 */

    uint32_t phoff = eh->e_phoff;
    uint16_t phnum = eh->e_phnum;
    uint16_t phentsize = eh->e_phentsize;
    if (phoff + (uint32_t)phnum * phentsize > (uint32_t)r) return -1;

    uint32_t min_vaddr = 0xFFFFFFFFu;
    uint32_t max_vaddr = 0;
    for (uint16_t i = 0; i < phnum; i++) {
        uint32_t off = phoff + i * phentsize;
        if (off + sizeof(Elf32_Phdr) > (uint32_t)r) return -1;
        Elf32_Phdr ph;
        memcpy_small(&ph, file_buf + off, sizeof(Elf32_Phdr));
        if (ph.p_type != 1) continue; /* PT_LOAD */
        if (ph.p_memsz == 0) continue;
        if (ph.p_vaddr < min_vaddr) min_vaddr = ph.p_vaddr;
        uint32_t end = ph.p_vaddr + ph.p_memsz;
        if (end > max_vaddr) max_vaddr = end;
    }
    if (min_vaddr == 0xFFFFFFFFu) return -1; /* no loadable segments */

    uint32_t total_size = max_vaddr - min_vaddr;
    if (total_size > RUN_BUF_SIZE) return -1;

    /* zero run buffer then copy segments from file_buf into run_buf (non-overlapping) */
    memset_small(run_buf, 0, total_size);
    for (uint16_t i = 0; i < phnum; i++) {
        uint32_t off = phoff + i * phentsize;
        Elf32_Phdr ph;
        memcpy_small(&ph, file_buf + off, sizeof(Elf32_Phdr));
        if (ph.p_type != 1) continue;
        if (ph.p_filesz > 0) {
            if (ph.p_offset + ph.p_filesz > (uint32_t)r) return -1;
            uint32_t dest = ph.p_vaddr - min_vaddr;
            memcpy_small(run_buf + dest, file_buf + ph.p_offset, ph.p_filesz);
        }
        /* remaining p_memsz already zeroed */
    }

    uint32_t entry = eh->e_entry;
    if (entry < min_vaddr || entry >= max_vaddr) return -1;
    uint32_t entry_off = entry - min_vaddr;
    void (*entry_point)(void) = (void(*)(void))(run_buf + entry_off);
    entry_point();
    return 0;
}

/* read file contents into buf up to bufsize */
int fs_read_file(const char *name, void *buf, int bufsize) {
    if (!fs_ready) return -1;
    fs_dirent_t d;
    if (dir_find(name, &d) < 0) return -1;
    uint32_t toread = d.size;
    if ((uint32_t)bufsize < toread) toread = bufsize;
    uint32_t blocks = (toread + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint8_t tmp[512];
    for (uint32_t b = 0; b < blocks; b++) {
        if (read_sector(d.start_block + b, tmp) != 0) return -1;
        uint32_t copy = (toread > 512) ? 512 : toread;
        memcpy_small((uint8_t*)buf + b * 512, tmp, copy);
        toread -= copy;
    }
    return (int)((d.size < (uint32_t)bufsize) ? d.size : bufsize);
}

/* remove file (free dir entry + bitmap) */
int fs_remove(const char *name) {
    if (!fs_ready) return -1;
    fs_dirent_t ent;
    int idx = dir_find(name, &ent);
    if (idx < 0) return -1;
    uint32_t blocks = (ent.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    for (uint32_t b = 0; b < blocks; b++) bitmap_set_block(ent.start_block + b, 0);
    uint32_t entries_per_sector = 512 / sizeof(fs_dirent_t);
    uint32_t sector = FS_ROOT_LBA + (idx / entries_per_sector);
    if (read_sector(sector, sector_buf) != 0) return -1;
    fs_dirent_t *ents = (fs_dirent_t*)sector_buf;
    int local_i = idx % entries_per_sector;
    memset_small(&ents[local_i], 0, sizeof(fs_dirent_t));
    if (write_sector(sector, sector_buf) != 0) return -1;
    return 0;
}

/* count files in root directory (simple helper) */
int fs_count_files(void) {
    if (!fs_ready) return 0;
    int count = 0;
    uint32_t lba = FS_ROOT_LBA;
    for (int s = 0; s < FS_ROOT_SECTS; s++) {
        if (read_sector(lba + s, sector_buf) != 0) return count;
        fs_dirent_t *ents = (fs_dirent_t*)sector_buf;
        int entries = 512 / sizeof(fs_dirent_t);
        for (int i = 0; i < entries; i++) if (ents[i].used) count++;
    }
    return count;
}
