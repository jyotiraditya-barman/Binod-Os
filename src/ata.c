/* ata.c - PIO read/write for primary master (0x1F0).
   Minimal and synchronous. Assumes interrupts disabled when used.
*/

#include "ata.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void insw(uint16_t port, void *addr, int cnt) {
    __asm__ volatile ("rep insw" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}
static inline void outsw(uint16_t port, const void *addr, int cnt) {
    __asm__ volatile ("rep outsw" : "+S"(addr), "+c"(cnt) : "d"(port));
}

/* Wait 400ns (reading alt status port 4 times) */
static void ata_delay() {
    inb(0x3F6);
    inb(0x3F6);
    inb(0x3F6);
    inb(0x3F6);
}

int ata_init(void) {
    // Basic identification removed — assume device present.
    return 0;
}

int ata_wait_busy(void) {
    /* wait until BSY clear and DRQ set */
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(0x1F7);
        if (!(status & 0x80) && (status & 8)) return 0; // not busy, DRQ set
    }
    return -1;
}

/* Read single sector LBA28 */
int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (lba > 0x0FFFFFFF) return -1;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F)); // drive & lba(27..24)
    outb(0x1F2, 1);   // sector count
    outb(0x1F3, (uint8_t)(lba & 0xFF));
    outb(0x1F4, (uint8_t)((lba >> 8) & 0xFF));
    outb(0x1F5, (uint8_t)((lba >> 16) & 0xFF));
    outb(0x1F7, 0x20); // READ PIO

    // wait for data
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(0x1F7);
        if (status & 8) break; // DRQ
        if (status & 1) return -1; // ERR
    }

    // read 256 words = 512 bytes
    insw(0x1F0, buffer, 256);
    ata_delay();
    return 0;
}

/* Write single sector LBA28 */
int ata_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (lba > 0x0FFFFFFF) return -1;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);   // sector count
    outb(0x1F3, (uint8_t)(lba & 0xFF));
    outb(0x1F4, (uint8_t)((lba >> 8) & 0xFF));
    outb(0x1F5, (uint8_t)((lba >> 16) & 0xFF));
    outb(0x1F7, 0x30); // WRITE PIO

    // wait for DRQ
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(0x1F7);
        if (status & 8) break; // DRQ
        if (status & 1) return -1; // ERR
    }
    outsw(0x1F0, buffer, 256);
    ata_delay();
    // flush cache
    outb(0x1F7, 0xE7);
    return 0;
}
