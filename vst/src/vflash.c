/**
 * vflash.c
 * Authors: Yun-Sheng Chang
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "config.h"
#include "vflash.h"
#include "vram.h"
#include "vpage.h"
#include "logger.h"
#include "checker.h"
#include "stat.h"

#define VST_UNKNOWN_CONTENT ((uint32_t)-1)

/* emulated DRAM and Flash memory */
//static flash_t flash;
static flash_t *flash_p;
static int sim_crash;
static int n_write_erase_ops;
static int p_write_erase_ops = 100000;

/* macro functions */
#define get_page(bank, blk, page) \
        (flash_p->banks[(bank)].blocks[(blk)].pages[(page)])

/* public interfaces */
/* flash memory APIs */
void vst_read_page(uint32_t bank, uint32_t blk, uint32_t page,
                   uint32_t sect, uint32_t n_sect, uint64_t dram_addr,
                   uint8_t *spare)
{
    record(LOG_FLASH, "R: flash(%u, %u, %u, %u, %u) -> mem[0x%lx] + sec[%u]\n",
            bank, blk, page, sect, n_sect, dram_addr, sect);
    inc_flash_read(1);

    assert(bank < VST_NUM_BANKS);
    assert(blk < VST_BLOCKS_PER_BANK);
    assert(page < VST_PAGES_PER_BLOCK);
    /* hardware requirement */
    assert(!(dram_addr % VST_BYTES_PER_SECTOR));

    flash_page_t *pp = &get_page(bank, blk, page);

    vpage_copy(vram_vpage_map(dram_addr), &pp->vpage, sect, n_sect);
    if (spare != NULL)
        memcpy(spare, pp->spare, SPARE_SIZE);
}

void vst_write_page(uint32_t bank, uint32_t blk, uint32_t page,
                    uint32_t sect, uint32_t n_sect, uint64_t dram_addr,
                    uint8_t *spare)
{
    record(LOG_FLASH, "W: mem[0x%lx] + sec[%u] -> flash(%u, %u, %u, %u, %u)\n",
            dram_addr, sect, bank, blk, page, sect, n_sect);
    inc_flash_write(1);

    assert(bank < VST_NUM_BANKS);
    assert(blk < VST_BLOCKS_PER_BANK);
    assert(page < VST_PAGES_PER_BLOCK);
    /* hardware requirement */
    assert(!(dram_addr % VST_BYTES_PER_SECTOR));

    chk_non_seq_write(flash_p, bank, blk, page);

    chk_overwrite(flash_p, bank, blk, page);

    flash_page_t *pp = &get_page(bank, blk, page);
    pp->is_erased = 0;
    vpage_copy(&pp->vpage, vram_vpage_map(dram_addr), sect, n_sect);
    if (spare != NULL)
        memcpy(pp->spare, spare, SPARE_SIZE);
    n_write_erase_ops++;
    if (sim_crash && n_write_erase_ops == p_write_erase_ops) {
        simulate_crash();
        n_write_erase_ops = 0;
    }
}

void vst_copyback_page(uint32_t bank, uint32_t blk_src, uint32_t page_src,
                       uint32_t blk_dst, uint32_t page_dst, uint8_t *spare)
{
    record(LOG_FLASH, "CB: flash(%u, %u, %u) -> flash(%u, %u, %u)\n",
            bank, blk_src, page_src,
            bank, blk_dst, page_dst);
    inc_flash_cb(1);

    assert(bank < VST_NUM_BANKS);
    assert(blk_src < VST_BLOCKS_PER_BANK);
    assert(page_src < VST_PAGES_PER_BLOCK);
    assert(blk_dst < VST_BLOCKS_PER_BANK);
    assert(page_dst < VST_PAGES_PER_BLOCK);

    chk_overwrite(flash_p, bank, blk_dst, page_dst);

    flash_page_t *pp_dst, *pp_src;
    pp_dst = &get_page(bank, blk_dst, page_dst);
    pp_src = &get_page(bank, blk_src, page_src);
    pp_dst->is_erased = 0;
    vpage_copy(&pp_dst->vpage, &pp_src->vpage, 0, VST_SECTORS_PER_PAGE);
    if (spare != NULL)
        memcpy(pp_dst->spare, spare, SPARE_SIZE);
    n_write_erase_ops++;
    if (sim_crash && n_write_erase_ops == p_write_erase_ops) {
        simulate_crash();
        n_write_erase_ops = 0;
    }
}

void vst_erase_block(uint32_t bank, uint32_t blk)
{
    record(LOG_FLASH, "E: flash(%u, %u)\n", bank, blk);
    inc_flash_erase(1);

    for (uint32_t i = 0; i < VST_PAGES_PER_BLOCK; i++) {
        flash_page_t *pp;
        pp = &get_page(bank, blk, i);
        pp->is_erased = 1;
        vpage_free(&pp->vpage);
        memset(pp->spare, 0xff, SPARE_SIZE);
    }
    n_write_erase_ops++;
    if (sim_crash && n_write_erase_ops == p_write_erase_ops) {
        simulate_crash();
        n_write_erase_ops = 0;
    }
}

int open_flash(int crash)
{
    uint32_t i, j, k;

    flash_p = calloc(1, sizeof(flash_t));
    if (flash_p == NULL) {
        fprintf(stderr, "Fail allocating virtual flash.\n");
        return 1;
    }
    for (i = 0; i < VST_NUM_BANKS; i++) {
        for (j = 0; j < VST_BLOCKS_PER_BANK; j++) {
            for (k = 0; k < VST_PAGES_PER_BLOCK; k++) {
                flash_page_t *pp = &get_page(i, j, k);
                pp->is_erased = 1;
                vpage_init(&pp->vpage, NULL);
                memset(pp->spare, 0xff, SPARE_SIZE);
            }
        }
    }
    sim_crash = crash;
    record(LOG_FLASH, "Virtual flash initialized\n");
    return 0;
}

void close_flash(void)
{
}

void serialize_flash(FILE *fp)
{
    uint32_t bank, blk, pg;

    for (bank = 0; bank < VST_NUM_BANKS; bank++) {
        for (blk = 0; blk < VST_BLOCKS_PER_BANK; blk++) {
            for (pg = 0; pg < VST_PAGES_PER_BLOCK; pg++) {
                flash_page_t *pp = &get_page(bank, blk, pg);
                if (!pp->is_erased) {
                    fwrite(&bank, sizeof(uint32_t), 1, fp);
                    fwrite(&blk, sizeof(uint32_t), 1, fp);
                    fwrite(&pg, sizeof(uint32_t), 1, fp);
                    fwrite(pp->spare, sizeof(uint8_t), SPARE_SIZE, fp);
                    vpage_serialize(&pp->vpage, fp);
                }
            }
        }
    }
    bank = (uint32_t)-1;
    fwrite(&bank, sizeof(uint32_t), 1, fp);
    record(LOG_GENERAL, "Flash serialized.\n");
}

void deserialize_flash(FILE *fp)
{
    uint32_t bank, blk, pg;
    size_t ret;

    while (1) {
        ret = fread(&bank, sizeof(uint32_t), 1, fp);
        if (ret == 0) {
            fprintf(stderr, "Fail to read file during deserializing flash.\n");
            exit(1);
        }
        if (bank == (uint32_t)-1)
            break;
        ret = fread(&blk, sizeof(uint32_t), 1, fp);
        if (ret == 0) {
            fprintf(stderr, "Fail to read file during deserializing flash.\n");
            exit(1);
        }
        ret = fread(&pg, sizeof(uint32_t), 1, fp);
        if (ret == 0) {
            fprintf(stderr, "Fail to read file during deserializing flash.\n");
            exit(1);
        }
        flash_page_t *pp = &get_page(bank, blk, pg);
        pp->is_erased = 0;
        ret = fread(pp->spare, sizeof(uint8_t), SPARE_SIZE, fp);
        if (ret == 0) {
            fprintf(stderr, "Fail to read file during deserializing flash.\n");
            exit(1);
        }
        vpage_deserialize(&pp->vpage, fp);
    }
    record(LOG_GENERAL, "Flash deserialized.\n");
}
