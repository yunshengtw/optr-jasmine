/**
 * pgmap.c
 * Authors: Yun-Sheng Chang
 */

#include "ftl.h"
#include "blkmgr.h"
#include "pgmap.h"
#include "cache.h"
#include "board.h"

#define PGMAP_COMMIT_PG (ROUND_UP(ROUND_UP(PAGE_MAP_BYTES, BYTES_PER_PAGE) / BYTES_PER_PAGE, NUM_BANKS) / NUM_BANKS)

static UINT32 check_pgmap_commit(UINT32 const blk);
static void record_pgmap_commit(UINT32 const blk);

typedef struct {
    UINT32 active_ppns[NUM_REGIONS];
    UINT32 log_ppn;
} pgmap_t;

static pgmap_t pgmap[NUM_BANKS];
extern UINT32 g_epoch;

/* init_pgmap() must be called after init_blkmgr() is called */
void init_pgmap(void)
{
    mem_set_dram(PAGE_MAP_ADDR, 0, PAGE_MAP_BYTES);
    mem_set_dram(BLK_TIME_ADDR, 0, BLK_TIME_BYTES);

    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        for (UINT32 region = 0; region < NUM_REGIONS; region++)
            pgmap[bank].active_ppns[region] = get_and_inc_active_blk(bank, region) * PAGES_PER_VBLK;
        pgmap[bank].log_ppn = get_log_blk(bank) * PAGES_PER_VBLK;
    }

    pgmap_restore_map_table();
}

void pgmap_close(void)
{
    pgmap_persist_map_table();
}

void pgmap_dump_l2p(void)
{
    uart_printf("Dumping l2p table. (Only non-zero entry is dumped.)\n");
    for (UINT32 lpn = 0; lpn < NUM_LPAGES; lpn++)
        if (get_ppn(lpn))
            uart_printf("%u -> %u\n", lpn, get_ppn(lpn));
}

void set_ppn(UINT32 const lpn, UINT32 const ppn)
{
    write_dram_32(PAGE_MAP_ADDR + lpn * sizeof(UINT32), ppn);
}

UINT32 get_ppn(UINT32 const lpn)
{
    return read_dram_32(PAGE_MAP_ADDR + lpn * sizeof(UINT32));
}

void set_lpn(UINT32 const bank, UINT32 const region, UINT32 const page, UINT32 const lpn)
{
    write_dram_32(LPNS(bank, region, page), lpn);
}

UINT32 get_lpn(UINT32 const bank, UINT32 const region, UINT32 const page)
{
    return read_dram_32(LPNS(bank, region, page));
}

/**
 * The semantic of "active ppn" is the ppn of the active page, which is
 * a clean page for the nearest write to settle
 */
UINT32 get_active_ppn(UINT32 const bank, UINT32 const region)
{
    return pgmap[bank].active_ppns[region];
}

UINT32 get_and_inc_active_ppn(UINT32 const bank, UINT32 const region)
{
    UINT32 ppn;
    UINT32 blk;

    ppn = pgmap[bank].active_ppns[region];
    blk = ppn / PAGES_PER_VBLK;

    if (ppn % PAGES_PER_VBLK == PAGES_PER_VBLK - 1) {
        /* program p2l table into summary page, and clear in-sram p2l table */
        UINT32 blk_clean = get_and_inc_active_blk(bank, region);
        ppn = blk_clean * PAGES_PER_VBLK;

        UINT32 pos = 0;
        mem_copy(FTL_BUF(bank) + pos, LPNS(bank, region, 0),
                sizeof(UINT32) * PAGES_PER_VBLK);
        pos += PAGES_PER_VBLK * sizeof(UINT32);

        /* create link to next open block */
        write_dram_32(FTL_BUF(bank) + pos, blk_clean);
        pos += sizeof(UINT32);

        stall_cache(bank);
        wait_bank_free(bank);
        nand_page_ptprogram_sync(bank, blk, PAGES_PER_VBLK - 1, 0,
                ROUND_UP(pos, BYTES_PER_SECTOR) /
                BYTES_PER_SECTOR, FTL_BUF(bank));
        release_cache(bank);
        mem_set_dram(LPNS(bank, region, 0), 0, sizeof(UINT32) * PAGES_PER_VBLK);
        mem_copy(BLK_TIME(bank, blk_clean), &g_epoch, sizeof(UINT32));
    }

    ASSERT(ppn / PAGES_PER_VBLK < VBLKS_PER_BANK);
    ASSERT(ppn % PAGES_PER_VBLK != PAGES_PER_VBLK - 1);
    pgmap[bank].active_ppns[region] = ppn + 1;

    return ppn;
}

void pgmap_trim(UINT32 const lpn, UINT32 const n_pages)
{
    for (UINT32 p = lpn; p < lpn + n_pages; p++)
        set_ppn(p, 0);
}

UINT32 get_log_ppn(UINT32 const bank)
{
    UINT32 ppn;

    ppn = pgmap[bank].log_ppn;

    /**
     * The last page in every log block is not used, as we have to differentiate
     * whether a log block is full or empty.
     */
    if (ppn % PAGES_PER_VBLK == PAGES_PER_VBLK - 1)
        ppn = get_log_blk(bank) * PAGES_PER_VBLK;
    pgmap[bank].log_ppn = ppn + 1;

    return ppn;
}

void revert_log_ppn(UINT32 const bank)
{
    revert_log_blk(bank);
    UINT32 blk_log = get_log_blk(bank);
    pgmap[bank].log_ppn = blk_log * PAGES_PER_VBLK;
}

void pgmap_restore_map_table(void)
{
    uart_printf("Start restoring page map.\n");
    UINT32 epochs[2];
    epochs[0] = check_pgmap_commit(blkmgr_get_map_blk(0));
    blkmgr_toggle_map_blk_idx();
    epochs[1] = check_pgmap_commit(blkmgr_get_map_blk(0));
    uart_printf("Epoch of pgmap 1 = %llu, pgmap 2 = %llu\n", epochs[0], epochs[1]);
    if (!epochs[0] && !epochs[1]) {
        uart_printf("No page map found.\n");
        return;
    }
    if (epochs[0] > epochs[1]) {
        blkmgr_toggle_map_blk_idx();
        g_epoch = epochs[0];
    } else {
        g_epoch = epochs[1];
    }
    uart_printf("g_epoch set to %u.\n", g_epoch);

    UINT32 bank;
    UINT32 blks[NUM_BANKS];
    UINT32 pg = 0;
    for (bank = 0; bank < NUM_BANKS; bank++)
        blks[bank] = blkmgr_get_map_blk(bank);
    bank = 0;
    UINT32 addr = PAGE_MAP_ADDR;
    UINT32 addr_end = PAGE_MAP_ADDR + PAGE_MAP_BYTES;
    UINT32 size = BYTES_PER_PAGE;
    while (addr != addr_end) {
        if (addr + BYTES_PER_PAGE > addr_end)
            size = addr_end - addr;
        nand_page_ptread(bank, blks[bank], pg,
                0, size / BYTES_PER_SECTOR, addr, RETURN_ON_ISSUE);
        addr += size;
        bank = (bank + 1) % NUM_BANKS;
        if (bank == 0)
            pg++;
    }
    flash_finish();
    uart_printf("Restoring page map done.\n");
}

void pgmap_persist_map_table(void)
{
    uart_printf("Start persisting page map.\n");
    UINT32 bank;
    UINT32 blks[NUM_BANKS];
    UINT32 pg = 0;
    for (bank = 0; bank < NUM_BANKS; bank++)
        blks[bank] = blkmgr_get_map_blk(bank);

    bank = 0;
    UINT32 addr = PAGE_MAP_ADDR;
    UINT32 addr_end = PAGE_MAP_ADDR + PAGE_MAP_BYTES;
    UINT32 size = BYTES_PER_PAGE;
    while (addr != addr_end) {
        if (addr + BYTES_PER_PAGE > addr_end)
            size = addr_end - addr;
        nand_page_ptprogram(bank, blks[bank], pg,
                0, size / BYTES_PER_SECTOR, addr);
        addr += size;
        bank = (bank + 1) % NUM_BANKS;
        if (bank == 0)
            pg++;
    }
    record_pgmap_commit(blks[0]);
    flash_finish();
    blkmgr_toggle_map_blk_idx();
    blkmgr_erase_cur_map_blk();
    uart_printf("Persisting page map done.\n");
}

static UINT32 check_pgmap_commit(UINT32 const blk)
{
    UINT32 magic;
    nand_page_ptread(0, blk, PGMAP_COMMIT_PG, 0, 1,
            FTL_BUF(0), RETURN_WHEN_DONE);
    magic = read_dram_32(FTL_BUF(0));
    if (magic == 815) {
        UINT32 epoch;
        mem_copy(&epoch, FTL_BUF(0) + 4, sizeof(UINT32));
        return epoch;
    }
    return 0;
}

static void record_pgmap_commit(UINT32 const blk)
{
    UINT32 magic = 815;
    UINT32 epoch_commit = g_epoch - 1;
    mem_copy(FTL_BUF(0), &magic, sizeof(UINT32));
    mem_copy(FTL_BUF(0) + 4, &epoch_commit, sizeof(UINT32));
    nand_page_ptprogram(0, blk, PGMAP_COMMIT_PG, 0, 1, FTL_BUF(0));
}
