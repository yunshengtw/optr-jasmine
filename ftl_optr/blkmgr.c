/**
 * blkmgr.c
 * Authors: Yun-Sheng Chang
 */

#include "ftl.h"
#include "blkmgr.h"
#include "pgmap.h"
#include "cache.h"
#include "log.h"
#include "stat.h"

static void build_bad_blk_list(void);
static void manual_set_bad_blk(void);
static BOOL32 is_bad_block(UINT32 bank, UINT32 blk);
static void init_blk_list(void);
static UINT32 get_victim_blk(UINT32 const bank, UINT32 const region);
static void set_bad_blk_cnt(UINT32 const bank, UINT32 const cnt);
static void inc_bad_blk_cnt(UINT32 const bank);
static UINT32 get_bad_blk_cnt(UINT32 const bank);
static void set_blk_id_global(UINT32 const bank, UINT32 const id, UINT16 blk);
static void set_blk_id(UINT32 const bank, UINT32 const region, UINT32 const id, UINT16 blk);
static UINT16 get_blk_id(UINT32 const bank, UINT32 const region, UINT32 const id);
static void set_vcount(UINT32 const bank, UINT32 const blk, UINT16 vcount);
static UINT16 get_vcount(UINT32 const bank, UINT32 const blk);
static void erase_all_log_blks(void);

typedef struct {
    /* [tail, rsv) are GC-available used blocks */
    /* [rsv, head) are GC-unavailable used blocks */
    /* [head, tail) are free blocks */
    UINT32 rsv, head, tail;
    UINT32 free;
    UINT32 offset, size;
} blk_list_t;

typedef struct {
    UINT32 free_blk_cnt;
    UINT32 bad_blk_cnt;
    blk_list_t blk_lists[NUM_REGIONS];
    UINT32 blk_log;
    UINT32 blk_log_first, blk_log_last;
    UINT32 blks_map[2];
    UINT32 vt_blk;
} blkmgr_t;

static blkmgr_t blkmgr[NUM_BANKS];
static UINT8 map_blk_idx;
static UINT32 log_blk_cnt;
static UINT8 first_gc;
/* for GC, dont need to initialize */
static UINT32 lpns[PAGES_PER_VBLK];
extern UINT32 enable_gc_opt;

void init_blkmgr(void)
{
    mem_set_dram(BAD_BLK_BMP_ADDR, 0, BAD_BLK_BMP_BYTES);
    build_bad_blk_list();
    uart_printf("[init_blkmgr] Bad block built.\n");
    #ifndef VST
    erase_all_blks();
    uart_printf("[init_blkmgr] All blocks erased.\n");
    #endif

    mem_set_dram(VCOUNT_ADDR, 0, VCOUNT_BYTES);
    map_blk_idx = 0;
    log_blk_cnt = NUM_LOG_BLKS_PER_BANK * NUM_BANKS;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        blkmgr[bank].vt_blk = 0;
    first_gc = 1;

    init_blk_list();
    uart_printf("[init_blkmgr] Block list initialized.\n");
}

UINT32 blkmgr_get_map_blk(UINT32 const bank)
{
    return blkmgr[bank].blks_map[map_blk_idx];
}

void blkmgr_toggle_map_blk_idx(void)
{
    map_blk_idx = (map_blk_idx + 1) % 2;
}

void blkmgr_erase_cur_map_blk(void)
{
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        nand_block_erase(bank, blkmgr[bank].blks_map[map_blk_idx]);
}

void erase_all_blks(void)
{
    /* erase all blocks, except the first block */
    UINT32 cnt;

    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        cnt = 0;
        for (UINT32 blk = 1; blk < VBLKS_PER_BANK; blk++) {
            if (!is_bad_block(bank, blk)) {
                cnt++;
                #if OPTION_SHOW_ERASE_BLK_INFO
                uart_printf("bb (%u, %u)\n", bank, blk);
                #endif
                nand_block_erase(bank, blk);
                set_vcount(bank, blk, 0);
            } else {
                set_vcount(bank, blk, VC_MAX);
            }
        }
        #if OPTION_SHOW_ERASE_BLK_INFO
        uart_printf("[bad block cnt] Bank %u: %u.\n", bank, cnt);
        #endif
    }
}

UINT32 get_and_inc_active_blk(UINT32 const bank, UINT32 const region)
{
    blkmgr[bank].free_blk_cnt--;
    blkmgr[bank].blk_lists[region].free--;
    UINT32 blk_free;
    UINT32 head = blkmgr[bank].blk_lists[region].head;
    blk_free = get_blk_id(bank, region, head);
    blkmgr[bank].blk_lists[region].head =
            (head + 1) % blkmgr[bank].blk_lists[region].size;
    return blk_free;
}

UINT32 get_log_blk(UINT32 const bank)
{
    UINT32 blk_log = blkmgr[bank].blk_log;
    log_blk_cnt--;
    if (blk_log > blkmgr[bank].blk_log_last) {
        /* this should not happen */
        uart_printf("Running out of log blocks.\n");
        while (1)
            ;
    }
    do {
        blkmgr[bank].blk_log++;
    } while (is_bad_block(bank, blkmgr[bank].blk_log));
    return blk_log;
}

void revert_log_blk(UINT32 const bank)
{
    blkmgr[bank].blk_log = blkmgr[bank].blk_log_first;
}

UINT32 get_rsv_blk(UINT32 const bank, UINT32 const region)
{
    return get_blk_id(bank, region, blkmgr[bank].blk_lists[region].rsv);
}

UINT32 n_cur_rsv_blks(void)
{
    UINT32 n_rsv_blks = 0;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        for (UINT32 region = 0; region < NUM_REGIONS; region++) {
            UINT32 head = blkmgr[bank].blk_lists[region].head;
            UINT32 rsv = blkmgr[bank].blk_lists[region].rsv;
            UINT32 size = blkmgr[bank].blk_lists[region].size;
            n_rsv_blks += (head > rsv ? head - rsv : head + size - rsv);
        }
    }
    return n_rsv_blks;
}

void push_rsv(UINT32 const bank)
{
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        for (UINT32 region = 0; region < NUM_REGIONS; region++) {
            UINT32 head = blkmgr[bank].blk_lists[region].head;
            UINT32 size = blkmgr[bank].blk_lists[region].size;
            blkmgr[bank].blk_lists[region].rsv = (head + size - 1) % size;
        }
    }
}

void inc_vcount(UINT32 const bank, UINT32 const blk)
{
    UINT32 vcount = get_vcount(bank, blk);
    ASSERT(vcount + 1 < PAGES_PER_VBLK);
    set_vcount(bank, blk, vcount + 1);
}

void dec_vcount(UINT32 const bank, UINT32 const blk)
{
    UINT32 vcount = get_vcount(bank, blk);
    ASSERT(vcount != 0);
    set_vcount(bank, blk, vcount - 1);
}

UINT32 blkmgr_reach_batch_gc_threshold(void)
{
    UINT32 n_blks = 0;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        /* TODO: Currently, we only use region 1 */
        for (UINT32 region = 1; region < NUM_REGIONS; region++)
            if (blkmgr[bank].blk_lists[region].free < GC_THRESHOLD) {
                n_blks += (GC_THRESHOLD - blkmgr[bank].blk_lists[region].free);
            }
#if 0
    if (n_blks > BATCH_GC_THRESHOLD)
        uart_printf("Batch GC threshold reached.\n");
#endif
    return (n_blks > BATCH_GC_THRESHOLD);
}

UINT32 reach_gc_threshold(UINT32 const bank, UINT32 const region)
{
    return (blkmgr[bank].blk_lists[region].free < GC_THRESHOLD);
}

extern UINT32 verbose;
void garbage_collection(UINT32 const bank, UINT32 const region)
{
    UINT32 lpn;
    UINT32 vt_blk;
    UINT32 vt_page;
    UINT16 vcount;

    if (first_gc) {
        uart_printf("First GC.\n");
        first_gc = 0;
    }
    #if 0
    uart_printf("GC bank %u\n", bank);
    #endif
    ptimer_start();
    cache_collect_dirty_rate();

    if (blkmgr[bank].vt_blk) {
        stat_gc_erase_sync();
        nand_block_erase(bank, blkmgr[bank].vt_blk);
        blkmgr[bank].vt_blk = 0;
    }

    vt_blk = get_victim_blk(bank, region);
    vcount = get_vcount(bank, vt_blk);

    #if 0
    uart_printf("Vt blk = %u\n", vt_blk);
    #endif
    nand_page_ptread(bank, vt_blk, PAGES_PER_VBLK - 1, 0,
            ROUND_UP(sizeof(UINT32) * PAGES_PER_VBLK, BYTES_PER_SECTOR) /
            BYTES_PER_SECTOR, FTL_BUF(bank), RETURN_WHEN_DONE);
    mem_copy(lpns, FTL_BUF(bank),
            sizeof(UINT32) * PAGES_PER_VBLK);

    UINT32 n_valid = 0;
    for (vt_page = 0; vt_page < (PAGES_PER_VBLK - 1); vt_page++) {
        UINT32 ppn = vt_blk * PAGES_PER_VBLK + vt_page;
        lpn = lpns[vt_page];

        if (get_ppn(lpn) == ppn) {
            /* Valid pages in victim blocks always written to cold region. */
            UINT32 gc_ppn = get_and_inc_active_ppn(bank, NUM_REGIONS - 1);
            UINT32 gc_blk = gc_ppn / PAGES_PER_VBLK;
            UINT32 gc_page = gc_ppn % PAGES_PER_VBLK;
            set_ppn(lpn, gc_ppn);
            set_lpn(bank, NUM_REGIONS - 1, gc_page, lpn);
            set_vcount(bank, gc_blk, get_vcount(bank, gc_blk) + 1);
            log_insert_mapent(lpn, gc_ppn);
            n_valid++;

            #ifdef VST
            UINT8 spare[64];
            UINT32 gc_tag = (UINT32)-2;
            mem_copy(spare, &lpn, sizeof(UINT32));
            mem_copy(spare + 8, &gc_tag, sizeof(UINT32));
            set_spare(spare, 12);
            #endif

            if (!enable_gc_opt)
                nand_page_copyback(bank, vt_blk, vt_page,
                        gc_blk, gc_page);
        }
    }
    stat_gc_vcount(vcount);

    #if 0
    if (vcount != 0) {
        uart_printf("v (%u, %u)\n",
                n_valid, vcount);
    }
    #endif
    if (n_valid != vcount) {
        for (UINT32 i = 0; i < PAGES_PER_VBLK; i++)
            uart_printf("%u: %u\n", i, lpns[i]);
        uart_printf("n_valid = %u, vcount = %u\n", n_valid, vcount);
    }
    ASSERT(n_valid == vcount);
    /* Assertion fail: GC threshold set too large */
    ASSERT(n_valid < PAGES_PER_VBLK - 1);

    blkmgr[bank].vt_blk = vt_blk;

    /* update metadata */
    set_vcount(bank, vt_blk, 0);
    blkmgr[bank].free_blk_cnt++;
    blkmgr[bank].blk_lists[region].free++;

    /* update blk list */
    blkmgr[bank].blk_lists[region].tail =
            (blkmgr[bank].blk_lists[region].tail + 1) %
            blkmgr[bank].blk_lists[region].size;

    stat_record_gc(bank, ptimer_stop());
}

void blkmgr_erase_vt_blk(UINT32 const bank)
{
    if (_BSP_FSM(REAL_BANK(bank)) != BANK_IDLE)
        return;
    if (blkmgr[bank].vt_blk) {
        stat_gc_erase_async();
        nand_block_erase(bank, blkmgr[bank].vt_blk);
        blkmgr[bank].vt_blk = 0;
    }
}

UINT32 blkmgr_reach_log_reclaim_threshold(void)
{
    return (log_blk_cnt < 3);
    //return (log_blk_cnt <= 1 * NUM_BANKS);
    //return (log_blk_cnt < 50 * NUM_BANKS);
}

void blkmgr_reclaim_log(void)
{
    stat_reclaim_log();
    pgmap_persist_map_table();
    erase_all_log_blks();
    log_blk_cnt = NUM_LOG_BLKS_PER_BANK * NUM_BANKS;
}

UINT32 blkmgr_first_gc_triggered(void)
{
    return !first_gc;
}

static void build_bad_blk_list(void)
{
    #ifndef VST
    UINT32 bank, num_entries, result, vblk_offset;
    scan_list_t* scan_list = (scan_list_t*) TEMP_BUF_ADDR;

    disable_irq();

    flash_clear_irq();

    for (bank = 0; bank < NUM_BANKS; bank++) {
        SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
        SETREG(FCP_BANK, REAL_BANK(bank));
        SETREG(FCP_OPTION, FO_E);
        SETREG(FCP_DMA_ADDR, (UINT32) scan_list);
        SETREG(FCP_DMA_CNT, SCAN_LIST_SIZE);
        SETREG(FCP_COL, 0);
        SETREG(FCP_ROW_L(bank), SCAN_LIST_PAGE_OFFSET);
        SETREG(FCP_ROW_H(bank), SCAN_LIST_PAGE_OFFSET);

        SETREG(FCP_ISSUE, 0);
        while ((GETREG(WR_STAT) & 0x00000001) != 0);
        while (BSP_FSM(bank) != BANK_IDLE);

        num_entries = 0;
        result = OK;

        if (BSP_INTR(bank) & FIRQ_DATA_CORRUPT) {
            result = FAIL;
        } else
        {
            UINT32 i;

            num_entries = read_dram_16(&(scan_list->num_entries));

            if (num_entries > SCAN_LIST_ITEMS) {
                result = FAIL;
            } else {
                for (i = 0; i < num_entries; i++) {
                    UINT16 entry = read_dram_16(scan_list->list + i);
                    UINT16 pblk_offset = entry & 0x7FFF;
                    if (pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK) {
                        #if OPTION_REDUCED_CAPACITY == FALSE
                        result = FAIL;
                        #endif
                    } else {
                        write_dram_16(scan_list->list + i, pblk_offset);
                    }
                }
            }
        }

        if (result == FAIL)
            num_entries = 0;  // We cannot trust this scan list. Perhaps a software bug.
        else
            write_dram_16(&(scan_list->num_entries), 0);

        set_bad_blk_cnt(bank, 0);

        for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++) {
            BOOL32 bad = FALSE;

            #if OPTION_2_PLANE
            UINT32 pblk_offset;

            pblk_offset = vblk_offset * NUM_PLANES;

            // fix bug@jasmine v.1.1.0
            if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
                bad = TRUE;

            pblk_offset = vblk_offset * NUM_PLANES + 1;

            // fix bug@jasmine v.1.1.0
            if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
                bad = TRUE;
            #else
            // fix bug@jasmine v.1.1.0
            if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, vblk_offset) < num_entries + 1)
                bad = TRUE;
            #endif

            if (bad) {
                inc_bad_blk_cnt(bank);
                set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);
            }
        }
    }
    manual_set_bad_blk();
    #endif
}

static void manual_set_bad_blk(void)
{
    #ifndef VST
    UINT32 bank, vblk_offset;

    bank = 15;
    vblk_offset = 2075;
    inc_bad_blk_cnt(bank);
    set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);

    bank = 11;
    vblk_offset = 1982;
    inc_bad_blk_cnt(bank);
    set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);

    bank = 6;
    vblk_offset = 457;
    inc_bad_blk_cnt(bank);
    set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);

    bank = 3;
    vblk_offset = 1143;
    inc_bad_blk_cnt(bank);
    set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);
    #endif
}

static BOOL32 is_bad_block(UINT32 bank, UINT32 blk)
{
    return tst_bit_dram(BAD_BLK_BMP_ADDR +
            bank * (VBLKS_PER_BANK / 8 + 1), blk);
}

static void init_blk_list(void)
{
    UINT32 bank;
    UINT32 blk;

    for (bank = 0; bank < NUM_BANKS; bank++) {
        blkmgr[bank].free_blk_cnt = VBLKS_PER_BANK - get_bad_blk_cnt(bank);

        /* reserve bad block bitmap and misc. block */
        set_vcount(bank, 0, VC_MAX);
        blkmgr[bank].free_blk_cnt--;
        set_vcount(bank, 1, VC_MAX);
        blkmgr[bank].free_blk_cnt--;

        blk = 2;
        UINT32 n_map = 0;
        do {
            /* reserve map block */
            if (!is_bad_block(bank, blk)) {
                blkmgr[bank].blks_map[n_map] = blk;
                blkmgr[bank].free_blk_cnt--;
                n_map++;
            }
            blk++;
        } while (n_map != 2);

        UINT32 n_log = 0;
        do {
            if (!is_bad_block(bank, blk)) {
                if (n_log == 0) {
                    blkmgr[bank].blk_log = blk;
                    blkmgr[bank].blk_log_first = blk;
                } else if (n_log == NUM_LOG_BLKS_PER_BANK - 1) {
                    blkmgr[bank].blk_log_last = blk;
                }
                blkmgr[bank].free_blk_cnt--;
                n_log++;
            }
            blk++;
        } while (n_log != NUM_LOG_BLKS_PER_BANK);

        UINT32 id = 0;
        do {
            if (!is_bad_block(bank, blk)) {
                set_blk_id_global(bank, id, blk);
                id++;
            }
            blk++;
        } while (blk < VBLKS_PER_BANK);

        blkmgr[bank].blk_lists[0].offset = 0;
        //blkmgr[bank].blk_lists[0].size = id / 4;
        blkmgr[bank].blk_lists[0].size = 60;
        blkmgr[bank].blk_lists[0].free = blkmgr[bank].blk_lists[0].size;

        blkmgr[bank].blk_lists[1].offset = blkmgr[bank].blk_lists[0].size;
        blkmgr[bank].blk_lists[1].size = id - blkmgr[bank].blk_lists[0].size;
        blkmgr[bank].blk_lists[1].free = blkmgr[bank].blk_lists[1].size;

        for (UINT32 region = 0; region < NUM_REGIONS; region++) {
            blkmgr[bank].blk_lists[region].rsv = 0;
            blkmgr[bank].blk_lists[region].head = 0;
            blkmgr[bank].blk_lists[region].tail = 0;
        }
    }
}

#define GC_FLUSH_ADD 2
static UINT16 vcounts[VBLKS_PER_BANK] __attribute__((aligned(4)));
static UINT32 get_victim_blk(UINT32 const bank, UINT32 const region)
{
    UINT32 tail = blkmgr[bank].blk_lists[region].tail;
    UINT32 rsv = blkmgr[bank].blk_lists[region].rsv;
    UINT32 size = blkmgr[bank].blk_lists[region].size;
    UINT32 idx;
    UINT32 blk;
    UINT16 vcount, vcount_min;

    mem_copy(vcounts, VCOUNT_ADDR + (bank * VBLKS_PER_BANK) * sizeof(UINT16), VBLKS_PER_BANK * sizeof(UINT16));
    idx = tail;
    blk = get_blk_id(bank, region, idx);
    vcount_min = vcounts[blk];
    ASSERT(vcount_min < PAGES_PER_VBLK);
    for (UINT32 i = (idx + 1) % size; i != rsv; i = (i + 1) % size) {
        blk = get_blk_id(bank, region, i);
        vcount = vcounts[blk];
        ASSERT(vcount < PAGES_PER_VBLK);
        if (vcount < vcount_min) {
            vcount_min = vcount;
            idx = i;
        }
    }
    /* for collecting the number of contrained GC blocks */
    #if 1
    UINT32 head = blkmgr[bank].blk_lists[region].head;
    stat_total_gc_blks(head > tail ? head - tail : head + size - tail);
    stat_constrained_gc_blks(head > rsv ? head - rsv : head + size - rsv);
    #endif

    #if 0
    /* for evaluating GC efficiency degradation */
    UINT32 head = blkmgr[bank].blk_list.head;
    UINT32 degrade = 0;
    UINT32 idx_rsv = rsv;
    UINT32 blk_rsv = get_blk_id(bank, idx_rsv);
    UINT32 min_rsv = get_vcount(bank, blk_rsv);
    for (UINT32 i = (idx_rsv + 1) % size; i != head; i = (i + 1) % size) {
        blk_rsv = get_blk_id(bank, i);
        val = get_vcount(bank, blk_rsv);
        if (val < min_rsv) {
            min_rsv = val;
            idx_rsv = i;
        }
    }
    if (min_rsv < min)
        degrade = (min - min_rsv);
    stat_record_gc_degrade(degrade);
    #endif

    UINT32 tmp_blk, vt_blk;
    tmp_blk = get_blk_id(bank, region, tail);
    vt_blk = get_blk_id(bank, region, idx);

    /* swap */
    set_blk_id(bank, region, tail, vt_blk);
    set_blk_id(bank, region, idx, tmp_blk);

    return vt_blk;
}

static void set_bad_blk_cnt(UINT32 const bank, UINT32 const cnt)
{
    blkmgr[bank].bad_blk_cnt = cnt;
}

static void inc_bad_blk_cnt(UINT32 const bank)
{
    blkmgr[bank].bad_blk_cnt++;
}

static UINT32 get_bad_blk_cnt(UINT32 const bank)
{
    return blkmgr[bank].bad_blk_cnt;
}

static void set_blk_id_global(UINT32 const bank, UINT32 const id, UINT16 blk)
{
    write_dram_16(BLK_LIST_ADDR + (bank * VBLKS_PER_BANK + id) *
            sizeof(UINT16), blk);
}

static void set_blk_id(UINT32 const bank, UINT32 const region, UINT32 const id, UINT16 blk)
{
    UINT32 offset = blkmgr[bank].blk_lists[region].offset;
    write_dram_16(BLK_LIST_ADDR + (bank * VBLKS_PER_BANK + offset + id) *
            sizeof(UINT16), blk);
}

static UINT16 get_blk_id(UINT32 const bank, UINT32 const region, UINT32 const id)
{
    UINT32 offset = blkmgr[bank].blk_lists[region].offset;
    return read_dram_16(BLK_LIST_ADDR +
            (bank * VBLKS_PER_BANK + offset + id) * sizeof(UINT16));
}

static void set_vcount(UINT32 const bank, UINT32 const blk, UINT16 const vcount)
{
    ASSERT(vcount == VC_MAX || (vcount >= 0 && vcount <= PAGES_PER_VBLK));
    write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + blk) *
            sizeof(UINT16), vcount);
}

static UINT16 get_vcount(UINT32 const bank, UINT32 const blk)
{
    return read_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + blk) *
            sizeof(UINT16));
}

static void erase_all_log_blks(void)
{
    UINT32 blks[NUM_BANKS];
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        blks[bank] = blkmgr[bank].blk_log_first;

    for (UINT32 cnt_erase = 0; cnt_erase < NUM_LOG_BLKS_PER_BANK; cnt_erase++) {
        for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
            nand_block_erase(bank, blks[bank]);
            do {
                blks[bank]++;
            } while (get_vcount(bank, blks[bank]) == VC_MAX);
        }
    }
}
