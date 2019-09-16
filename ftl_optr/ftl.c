/**
 * ftl.c
 * Core FTL
 * Features:
 * 1. page-level mapping
 * 2. garbage collection
 * 3. write buffer and cache flushing
 */

#include <stdlib.h>
#include "ftl.h"
#include "blkmgr.h"
#include "pgmap.h"
#include "recovery.h"
#include "cache.h"
#include "log.h"
#include "stat.h"
#include "board.h"

static void init_dram(void);
static void load_metadata(void);
static void save_metadata(void);
static void sanity_check(void);
static void format(void);

UINT32 g_epoch;
UINT32 g_ftl_read_buf_id, g_ftl_write_buf_id;
UINT32 verbose;
UINT32 enable_gc_opt;

void ftl_open(void)
{
    led(0);
    uart_printf("ftl_open() starts\n");
    sanity_check();

    init_dram();
    uart_printf("Initializing DRAM done.\n");
    init_blkmgr();
    uart_printf("Initializing blkmgr done.\n");
    init_pgmap();
    uart_printf("Initializing pgmap done.\n");

    g_ftl_read_buf_id = 0;
    g_ftl_write_buf_id = 0;
    /* TODO: currently, this FTL cant save any data after reboot */
    #ifdef VST
    if (!check_format_mark()) {
    #else
    if (TRUE) {
    #endif
        g_epoch = 0;
        format();
    } else {
        load_metadata();
    }
    uart_printf("Format done.\n");

    init_cache();
    uart_printf("Initializing cache done.\n");

    flash_clear_irq();

    set_intr_mask(FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
    set_fconf_pause(FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
    uart_printf("Interrupt mask set.\n");

    enable_irq();
    uart_printf("IRQ enabled.\n");

    #ifndef VST
    /* 1 s */
    //start_timer(TIMER_CH2, TIMER_PRESCALE_2, 344828);
    /* 5 s */
    //start_timer(TIMER_CH2, TIMER_PRESCALE_2, 1724138);
    /* 10 s */
    //start_timer(TIMER_CH2, TIMER_PRESCALE_2, 3448276);
    /* 30 s */
    //start_timer(TIMER_CH2, TIMER_PRESCALE_2, 10344828);
    start_timer(TIMER_CH2, TIMER_PRESCALE_2, 344828 * AUTO_FLUSH);
    #endif

    uart_printf("ftl_open() ends\n");
    led(1);

    //pgmap_dump_l2p();

    uart_printf("Number of banks: %d\n", NUM_BANKS);
    uart_printf("Number of blocks per bank: %d\n", VBLKS_PER_BANK);
    uart_printf("Number of pages per block: %d\n", PAGES_PER_VBLK);
    uart_printf("Page size: %d bytes\n", BYTES_PER_PAGE);
    uart_printf("Number of write buffers: %d\n", NUM_WR_BUFFERS);
    uart_printf("Number of read buffers: %d\n", NUM_RD_BUFFERS);
    uart_printf("Number of cache buffers: %d\n", NUM_CACHE_BUFFERS);
    uart_printf("Number of cache buffers per banks: %d\n", NUM_CACHE_BUFFERS_PER_BANK);
    uart_printf("Number of blocks reserved: %d\n", NUM_BLKS_RSV);
    uart_printf("Number of mapents per page: %d\n", NUM_MAPENTS_PER_PAGE);
    uart_printf("Sanity check (mapents): %d < %d\n", NUM_MAPENTS_PER_PAGE * 8 + 8, BYTES_PER_PAGE);
    ASSERT(NUM_MAPENTS_PER_PAGE * 8 + 8 < BYTES_PER_PAGE);
    uart_printf("Sanity check (depents): %d < %d\n", NUM_DEPENTS_PER_PAGE * 12 + 8, BYTES_PER_PAGE);
    ASSERT(NUM_DEPENTS_PER_PAGE * 20 + 8 < BYTES_PER_PAGE);
    uart_printf("[optr] Auto-flush: %u s | GC batch: %u blks | Chkpt area: %u blks\n",
                AUTO_FLUSH, BATCH_GC_THRESHOLD, NUM_LOG_BLKS_PER_BANK * NUM_BANKS);

    UINT32 version = 10;
    uart_printf("Experimental FTL version %u\n", version);
}

void ftl_close(void)
{
    show_stat();
    #if 1
    //record_depent();
    ftl_flush();
    record_mapent();
    record_tag();
    #endif
    pgmap_close();
}

#define get_bank(lpn) ((lpn) % NUM_BANKS)
void ftl_read(UINT32 const lba, UINT32 const n_sect)
{
    UINT32 remain_sect, base_sect, cnt_sect;
    UINT32 lpn, ppn;
    UINT32 bank;

    stat_host_read(n_sect);
    #if 0
    cache_collect_dirty_rate();
    #endif

    remain_sect = n_sect;
    lpn = lba / SECTORS_PER_PAGE;
    base_sect = lba % SECTORS_PER_PAGE;

    #if 0
    uart_printf("r %d %d\n", lba, n_sect);
    #endif
    while (remain_sect != 0) {
        stat_record_para(num_busy_banks());

        if (base_sect + remain_sect < SECTORS_PER_PAGE)
            cnt_sect = remain_sect;
        else
            cnt_sect = SECTORS_PER_PAGE - base_sect;

        bank = get_bank(lpn);
        ppn = get_ppn(lpn);

        UINT32 buf_id;
        buf_id = exist_in_cache(bank, lpn);
        if (buf_id == -1) {
            if (ppn != 0) {
                stall_cache(bank);
                wait_bank_free(bank);
                nand_page_ptread_to_host(bank, ppn / PAGES_PER_VBLK, ppn % PAGES_PER_VBLK,
                        base_sect, cnt_sect);
                release_cache(bank);
            } else {
                /* try to read a logical page that has never been written to */
                UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
                wait_rdbuf_free(next_read_buf_id);
                #ifdef VST
                omit_next_dram_op();
                #endif
                mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) +
                        base_sect * BYTES_PER_SECTOR,
                        0xffffffff, cnt_sect * BYTES_PER_SECTOR);
                flash_finish();
                set_bm_read_limit(next_read_buf_id);
                g_ftl_read_buf_id = next_read_buf_id;
            }
        } else {
            #if 0
            uart_printf("R hit %u\n", lpn);
            #endif
            wait_buf_complete(bank, buf_id);
            UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
            wait_rdbuf_free(next_read_buf_id);
            mem_copy(RD_BUF_PTR(g_ftl_read_buf_id) + base_sect * BYTES_PER_SECTOR,
                    CACHE_BUF(bank, buf_id) + base_sect * BYTES_PER_SECTOR,
                    cnt_sect * BYTES_PER_SECTOR);
            flash_finish();
            set_bm_read_limit(next_read_buf_id);
            g_ftl_read_buf_id = next_read_buf_id;
        }

        base_sect = 0;
        remain_sect -= cnt_sect;
        lpn++;
    }
}

UINT16 g_pg_span;
void ftl_write(UINT32 const lba, UINT32 const n_sect)
{
    UINT32 remain_sect, base_sect, cnt_sect;
    UINT32 lpn;
    UINT32 bank;
    UINT32 comp;
    UINT32 total_dirty_bufs;

    if (!n_sect)
        return;

    stat_host_write(n_sect);
    #if 0
    cache_collect_dirty_rate();
    #endif

    remain_sect = n_sect;
    lpn = lba / SECTORS_PER_PAGE;
    base_sect = lba % SECTORS_PER_PAGE;

    UINT32 lpn_end = (lba + n_sect - 1) / SECTORS_PER_PAGE;
    g_pg_span = lpn_end - lpn + 1;

    #if 0
    uart_printf("w %d %d\n", lba, n_sect);
    #endif
    while (remain_sect != 0) {
        pool_write_buf();
        stat_record_para(num_busy_banks());

        if (base_sect + remain_sect < SECTORS_PER_PAGE)
            cnt_sect = remain_sect;
        else
            cnt_sect = SECTORS_PER_PAGE - base_sect;

        bank = get_bank(lpn);

        /* wait until data reach sata write buffer */
        while (!is_write_buf_valid() && !enable_gc_opt) {
            #if 0
            uart_printf("Wait for sata: %u %u %u\n",
                GETREG(BM_WRITE_LIMIT),
                g_ftl_write_buf_id,
                GETREG(SATA_WBUF_PTR)
            );
            #endif
        }

        UINT32 buf_id;
        comp = 1;
        buf_id = exist_in_cache(bank, lpn);
        /* no existing entry with same lpn */
        if (buf_id == -1) {
            buf_id = get_clean_cache_buf(bank);
            UINT32 ppn = get_ppn(lpn);
            if (ppn != 0) {
                if (cnt_sect == SECTORS_PER_PAGE) {
                    stat_record_full_write(bank);
                    #if 0
                    uart_printf("Full W bk %u lpn %u buf %u\n", bank, lpn, buf_id);
                    #endif
                } else {
                    if (base_sect == 0 || (base_sect + cnt_sect) % SECTORS_PER_PAGE == 0) {
                        comp = 0;
                        UINT32 base_preread;
                        if (base_sect == 0)
                            base_preread = cnt_sect;
                        else
                            base_preread = 0;
                        UINT32 cnt_preread = SECTORS_PER_PAGE - cnt_sect;
                        stat_record_update_side(bank);
                        stall_cache(bank);
                        wait_bank_free(bank);
                        nand_page_ptread(bank,
                                ppn / PAGES_PER_VBLK, ppn % PAGES_PER_VBLK,
                                base_preread, cnt_preread,
                                CACHE_BUF(bank, buf_id), RETURN_ON_ISSUE);
                        release_cache(bank);
                    }
                    else {
                        stat_record_update_center(bank);
                        stall_cache(bank);
                        wait_bank_free(bank);
                        nand_page_ptread(bank,
                                ppn / PAGES_PER_VBLK, ppn % PAGES_PER_VBLK,
                                0, SECTORS_PER_PAGE,
                                //CACHE_BUF(bank, buf_id), RETURN_WHEN_DONE);
                                CACHE_BUF(bank, buf_id), RETURN_ON_ISSUE);
                        wait_wr_free();
                        wait_bank_free(bank);
                        release_cache(bank);
                    }
                    #if 0
                    uart_printf("Preread bk %u lpn %u buf %u\n", bank, lpn, buf_id);
                    #endif
                }
            } else {
                stat_record_insert(bank);
                #if 0
                uart_printf("New write bk %u lpn %u buf %u\n", bank, lpn, buf_id);
                #endif
            }
        } else {
            if (is_cache_ent_dirty(bank, buf_id)) {
                UINT32 epoch_src = get_cache_ent_epoch(bank, buf_id);
                UINT16 pg_span = get_cache_ent_pg_span(bank, buf_id);
                insert_dep_entry(epoch_src, pg_span);
                if (is_depents_full())
                    record_depent();
            }
            stat_record_merge_write(bank);
            #if 0
            uart_printf("Merge bk %u lpn %u buf %u\n", bank, lpn, buf_id);
            #endif
        }

        enqueue(bank, lpn, buf_id, base_sect,
                SECTORS_PER_PAGE - (base_sect + cnt_sect), comp);

        base_sect = 0;
        remain_sect -= cnt_sect;
        lpn++;
    }

    if (blkmgr_reach_batch_gc_threshold()) {
        total_dirty_bufs = ftl_prefix_flush();
        stat_gc_flush_page(total_dirty_bufs);
        UINT32 done_gc = 0;
        while (!done_gc) {
            done_gc = 1;
            /* TODO: Currently, we only use region 1 */
            for (UINT32 region = 1; region < NUM_REGIONS; region++) {
                for (bank = 0; bank < NUM_BANKS; bank++) {
                    if (reach_gc_threshold(bank, region)) {
                        garbage_collection(bank, region);
                    }
                }
            }

            /* TODO: Currently, we only use region 1 */
            for (UINT32 region = 1; region < NUM_REGIONS; region++) {
                for (bank = 0; bank < NUM_BANKS; bank++) {
                    if (reach_gc_threshold(bank, region)) {
                        done_gc = 0;
                    }
                }
            }
        }
    }
    g_epoch++;

    if (reach_chkpt_threshold()) {
        stat_record_chkpt();
        total_dirty_bufs = ftl_prefix_flush();
        stat_chkpt_flush_page(total_dirty_bufs);
        record_mapent();
        record_tag();
    }

    if (reach_flush_depent()) {
        UINT32 total_dirty_bufs;
        total_dirty_bufs = ftl_prefix_flush();
        stat_manual_flush_page(total_dirty_bufs);
    }

    stat_periodic_show_stat();
}

void ftl_flush(void)
{
    #if 0
    uart_printf("f\n");
    #endif
    ptimer_start();
    ftl_prefix_flush();
    stat_record_flush(ptimer_stop());
}

UINT32 ftl_prefix_flush(void)
{
    UINT32 total_dirty_bufs = cache_get_total_dirty_bufs();
    record_depent();
    flush_write_buf();
    return total_dirty_bufs;
}

void ftl_standby(void)
{
    show_stat();
    verbose = !verbose;
    if (verbose)
        uart_printf("Verbose on.\n");
    else
        uart_printf("Verbose off.\n");
}

void ftl_idle(void)
{
    uart_printf("Start warming up to trigger GC.\n");
    enable_gc_opt = 1;
    srand(815);
    UINT32 footprint = 100 * 1024 * 1024;
    /* 100M sectors = 50GiB */
    UINT32 gran = SECTORS_PER_PAGE * NUM_BANKS;
    UINT32 amnt_written = 0;
    while (!blkmgr_first_gc_triggered()) {
        UINT32 lba = (rand() % footprint) / gran * gran;
        ftl_write(lba, gran);
        amnt_written += gran;
        if (!(amnt_written % (8 * 1024 * 1024))) {
            uart_printf("%u GB written.\n", amnt_written / 2 / 1024 / 1024);
        }
    }
    /**
     * When the first GC is triggered, it may only trigger a few banks' GC
     * due to imbalance number of bad blocks.
     * Thus, we continue to write 4 GB of data to trigger all banks' GC.
     */
    for (UINT32 i = 0; i < (8 * 1024 * 1024) / gran; i++) {
        UINT32 lba = (rand() % (8 * 1024 * 1024)) / gran * gran;
        ftl_write(lba, gran);
    }
    enable_gc_opt = 0;
    uart_printf("GC triggered, ready to accept requests.\n");
}

void ftl_trim(UINT32 const reserved, UINT32 const n_range_ents)
{
    #if 0
    uart_printf("t %u %u\n", reserved, n_range_ents);
    #endif
    UINT32 lba, n_sects;
    for (UINT32 i = 0; i < n_range_ents; i++) {
        while (!is_write_buf_valid())
            ;
        for (UINT32 j = 0; j < 512 / 8; j++) {
            lba = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) +
                    (2 * j) * sizeof(UINT32));
            n_sects = read_dram_32(WR_BUF_PTR(g_ftl_write_buf_id) +
                    (2 * j + 1) * sizeof(UINT32));
            n_sects = n_sects >> 16;
            UINT32 lpn = (lba + SECTORS_PER_PAGE - 1) / SECTORS_PER_PAGE;
            UINT32 lpn_end = (lba + n_sects) / SECTORS_PER_PAGE;
            UINT32 pg = lpn_end - lpn;

            pgmap_trim(lpn, pg);
        }

        g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
        SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);
        SETREG(BM_STACK_RESET, 0x01);
    }
}

UINT32 ftl_get_epoch_incomplete(void)
{
    return recovery_get_epoch_incomplete();
}

void ftl_recovery(void)
{
    init_recovery();
    int done;
    done = analyze();
    if (!done)
        rebuild();
}

UINT32 ftl_get_epoch(void)
{
    return g_epoch;
}

static void init_dram(void)
{
    mem_set_dram(DEP_BUF_ADDR, 0, DEP_BUF_BYTES);
    mem_set_dram(BAD_BLK_BMP_ADDR, 0, BAD_BLK_BMP_BYTES);
    mem_set_dram(PAGE_MAP_ADDR, 0, PAGE_MAP_BYTES);
    mem_set_dram(LPNS_ADDR, 0, LPNS_BYTES);
    mem_set_dram(VCOUNT_ADDR, 0, VCOUNT_BYTES);
    mem_set_dram(EPOCHS_ADDR, 0, EPOCHS_BYTES);
    mem_set_dram(BLK_LIST_ADDR, 0, BLK_LIST_BYTES);
    mem_set_dram(BLK_TIME_ADDR, 0, BLK_TIME_BYTES);
}

static void load_metadata(void)
{
    uart_printf("Used SSD, start loading metadata.\n");

    /* init dram */
    //mem_set_dram(PAGE_MAP_ADDR, 0, PAGE_MAP_BYTES);
    //mem_set_dram(VCOUNT_ADDR, 0, VCOUNT_BYTES);

    ftl_recovery();
}

static void save_metadata(void)
{
}

static void sanity_check(void)
{
    ASSERT(RECOVERY_PAGE_EPOCH_ADDR + RECOVERY_PAGE_EPOCH_BYTES < DEP_BUF_ADDR);
}

static void format(void)
{
    uart_printf("New SSD, start formating.\n");

    erase_all_blks();

    save_metadata();

    write_format_mark();
    record_tag();
}
