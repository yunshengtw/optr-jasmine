/**
 * vflash.h
 * Authors: Yun-Sheng Chang
 */

#ifndef VFLASH_H
#define VFLASH_H

#include <stdint.h>
#include "config.h"
#include "vpage.h"

#define SPARE_SIZE 64
typedef struct {
    uint8_t is_erased;
    vpage_t vpage;
    uint8_t spare[SPARE_SIZE];
} flash_page_t;

typedef struct {
    flash_page_t pages[VST_PAGES_PER_BLOCK];
} flash_block_t;

typedef struct {
    flash_block_t blocks[VST_BLOCKS_PER_BANK];
} flash_bank_t;

typedef struct {
    flash_bank_t banks[VST_NUM_BANKS];
} flash_t;

/* flash memory APIs */
void vst_read_page(uint32_t bank, uint32_t blk, uint32_t page, uint32_t sect,
                   uint32_t n_sect, uint64_t dram_addr,
                   uint8_t *spare);
void vst_write_page(uint32_t bank, uint32_t blk, uint32_t page,
                    uint32_t sect, uint32_t n_sect, uint64_t dram_addr,
                    uint8_t *spare);
void vst_copyback_page(uint32_t bank, uint32_t blk_src, uint32_t page_src,
                       uint32_t blk_dst, uint32_t page_dst, uint8_t *spare);
void vst_erase_block(uint32_t bank, uint32_t blk);

int open_flash(int crash);
void close_flash(void);
void serialize_flash(FILE *fp);
void deserialize_flash(FILE *fp);

#endif // VFLASH_H
