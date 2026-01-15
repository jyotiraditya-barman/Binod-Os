#ifndef FS_H
#define FS_H
#include <stdint.h>

#define FS_MAGIC 0x42494E4F /* 'BINO' */
#define FS_SECTOR 512

int fs_init(void);
int fs_format_hostimage(const char *imgpath); /* host utility uses mkfs, not in kernel */
int fs_list(void);
int fs_read_file(const char *name, void *buf, int bufsize);
int fs_write_file(const char *name, const void *data, int size);
int fs_remove(const char *name);
int fs_run(const char *name);
int fs_count_files(void);

#endif
