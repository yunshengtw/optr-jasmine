/**
 * log.c
 * Authors: Yun-Sheng Chang
 */

#include "ftl.h"
#include "blkmgr.h"
#include "pgmap.h"
#include "cache.h"
#include "log.h"
#include "stat.h"

static void set_chkpt_ent(UINT32 const bank, UINT32 const n_valid,
                          UINT32 const lpn, UINT32 const ppn);
static void set_mapent(UINT32 const idx, UINT32 const lpn, UINT32 const ppn);
static void set_dep_entry(UINT32 const idx, UINT32 const src,
                          UINT32 const dst, UINT16 const req_size);
static void persist_mapent(UINT32 const bank_chkpt, UINT32 const idx,
                           UINT32 const cnt);
static void persist_tag(UINT32 const bank_chkpt);
static void persist_depent(UINT32 const cnt_deps, UINT32 const bank_chkpt);

typedef struct {
    UINT32 cnt_deps;
    UINT32 cnt_mapents;
    UINT32 bank_active;
    UINT32 require_flush_depent;
} chkpt_t;

static chkpt_t chkpt;

void init_log(void)
{
    chkpt.cnt_deps = 0;
    chkpt.cnt_mapents = 0;
    chkpt.bank_active = 0;
    chkpt.require_flush_depent = 0;
}

extern UINT32 g_epoch;
void insert_dep_entry(UINT32 const epoch_src, UINT16 const pg_span)
{
    set_dep_entry(chkpt.cnt_deps, epoch_src, g_epoch, pg_span);
    chkpt.cnt_deps++;
}

UINT32 is_depents_full(void)
{
    return (chkpt.cnt_deps >= NUM_DEPENTS_PER_PAGE);
}

void log_insert_mapent(UINT32 const lpn, UINT32 const ppn)
{
    set_mapent(chkpt.cnt_mapents, lpn, ppn);
    chkpt.cnt_mapents++;
}

UINT32 reach_chkpt_threshold(void)
{
    /* 512 is an arbitrary number to fully utilize the last mapent page */
    return (chkpt.cnt_mapents > (NUM_BANKS - 1) * NUM_MAPENTS_PER_PAGE - 512 ||
            blkmgr_reach_log_reclaim_threshold());
}

static UINT32 pg_have_used;
/* Invoke ftl_flush() before this method */
void record_mapent(void)
{
    UINT32 pg;
    UINT32 idx = 0;
    UINT32 pg_used = 0;
    UINT32 bank_chkpt = chkpt.bank_active;
    while (chkpt.cnt_mapents) {
        pg = chkpt.cnt_mapents > NUM_MAPENTS_PER_PAGE ?
             NUM_MAPENTS_PER_PAGE : chkpt.cnt_mapents;
        persist_mapent(bank_chkpt, idx, pg);
        idx += pg;
        chkpt.cnt_mapents -= pg;
        pg_used++;
        bank_chkpt = (bank_chkpt + 1) % NUM_BANKS;
    }

    pg_have_used += pg_used;
    chkpt.bank_active = bank_chkpt;

    #if 0
    uart_printf("Record map ent done. Use %u pgs. Have used %u pgs\n",
            pg_used, pg_have_used);
    #endif
}

void record_tag(void)
{
    UINT32 bank_chkpt = chkpt.bank_active;
    UINT32 pg_used = 0;

    /**
     * When this function is called upon format(), one must ensures it's
     * called after init_pgmap() so that the reserve blocks will not become -1.
     */
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        push_rsv(bank);

    flash_finish();
    persist_tag(bank_chkpt);
    bank_chkpt = (bank_chkpt + 1) % NUM_BANKS;
    pg_used++;
    flash_finish();

    pg_have_used += pg_used;
    chkpt.bank_active = bank_chkpt;

    #if 0
    uart_printf("Record commit tag done. Use %u pgs. Have used %u pgs\n",
            pg_used, pg_have_used);
    #endif

    if (blkmgr_reach_log_reclaim_threshold()) {
        ptimer_start();
        blkmgr_reclaim_log();
        uart_printf("Reclaim log takes %u (usec)\n", ptimer_stop());
        for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
            revert_log_ppn(bank);
        chkpt.bank_active = 0;
        record_tag();
    }
}

void record_depent(void)
{
    chkpt.require_flush_depent = 0;
    if (!chkpt.cnt_deps)
        return;
    stat_record_dep(chkpt.cnt_deps);
    UINT32 bank_chkpt = chkpt.bank_active;
    UINT32 pg_used = 0;

    persist_depent(chkpt.cnt_deps, bank_chkpt);
    bank_chkpt = (bank_chkpt + 1) % NUM_BANKS;
    pg_used++;
    
    pg_have_used += pg_used;
    chkpt.bank_active = bank_chkpt;
    chkpt.cnt_deps = 0;

    #if 0
    uart_printf("Record dep ent done. Use %u pgs. Have used %u pgs\n",
            pg_used, pg_have_used);
    #endif
}

UINT32 reach_flush_depent(void)
{
    return chkpt.require_flush_depent;
}

void schedule_flush_depent(void)
{
    chkpt.require_flush_depent = 1;
}

static void set_chkpt_ent(UINT32 const bank, UINT32 const n_valid,
                          UINT32 const lpn, UINT32 const ppn)
{
    UINT32 base = HEAD_BUF(bank) + sizeof(UINT32) + sizeof(UINT32) + n_valid * (2 * sizeof(UINT32));
    write_dram_32(base, lpn);
    write_dram_32(base + 4, ppn);
}

static void set_mapent(UINT32 const idx, UINT32 const lpn, UINT32 const ppn)
{
    ASSERT(idx < 2 * NUM_BANKS * NUM_MAPENTS_PER_PAGE);
    UINT32 base = CHKPT_BUF(idx / NUM_MAPENTS_PER_PAGE) +
                  sizeof(UINT32) + sizeof(UINT32) +
                  (idx % NUM_MAPENTS_PER_PAGE) * (2 * sizeof(UINT32));
    write_dram_32(base, lpn);
    write_dram_32(base + 4, ppn);
}

static void set_dep_entry(UINT32 const idx, UINT32 const src,
                          UINT32 const dst, UINT16 const req_size)
{
    UINT32 base = DEP_BUF_ADDR + sizeof(UINT32) + sizeof(UINT32) + idx * (2 * sizeof(UINT32) + sizeof(UINT32));
    mem_copy(base, &src, sizeof(UINT32));
    mem_copy(base + 4, &dst, sizeof(UINT32));
    write_dram_32(base + 8, req_size);
}

static void persist_mapent(UINT32 const bank_chkpt, UINT32 const idx,
                           UINT32 const cnt)
{
    write_dram_32(CHKPT_BUF(idx / NUM_MAPENTS_PER_PAGE), 200);
    write_dram_32(CHKPT_BUF(idx / NUM_MAPENTS_PER_PAGE) + 4, cnt);
    UINT32 ppn_chkpt = get_log_ppn(bank_chkpt);
    #if 0
    uart_printf("record at %u %u\n", bank_chkpt, ppn_chkpt);
    #endif
    stat_chkpt_page();
    nand_page_program(
            bank_chkpt,
            ppn_chkpt / PAGES_PER_VBLK, ppn_chkpt % PAGES_PER_VBLK,
            CHKPT_BUF(idx / NUM_MAPENTS_PER_PAGE)
    );
}

/* order is preserved by bank and blk id */
static void persist_tag(UINT32 const bank_chkpt)
{
    UINT32 epoch_latest = g_epoch - 1;
    write_dram_32(HEAD_BUF(bank_chkpt), 100);
    mem_copy(HEAD_BUF(bank_chkpt) + 4, &epoch_latest, sizeof(UINT32));
    UINT32 active_ppns[NUM_BANKS][NUM_REGIONS];
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        for (UINT32 region = 0; region < NUM_REGIONS; region++)
            active_ppns[bank][region] = get_active_ppn(bank, region);
    mem_copy(HEAD_BUF(bank_chkpt) + 8, active_ppns, sizeof(active_ppns));
    UINT32 ppn_chkpt = get_log_ppn(bank_chkpt);
    #if 0
    uart_printf("tag at %u %u\n", bank_chkpt, ppn_chkpt);
    #endif
    stat_tag_page();
    nand_page_program(
            bank_chkpt,
            ppn_chkpt / PAGES_PER_VBLK, ppn_chkpt % PAGES_PER_VBLK,
            HEAD_BUF(bank_chkpt)
    );
}

static void persist_depent(UINT32 const cnt_deps, UINT32 const bank_chkpt)
{
    write_dram_32(DEP_BUF_ADDR, 300);
    write_dram_32(DEP_BUF_ADDR + 4, cnt_deps);
    UINT32 ppn_chkpt = get_log_ppn(bank_chkpt);
    #if 0
    uart_printf("dep at %u %u\n", bank_chkpt, ppn_chkpt);
    #endif
    stat_dep_page();
    nand_page_program(
            bank_chkpt,
            ppn_chkpt / PAGES_PER_VBLK, ppn_chkpt % PAGES_PER_VBLK,
            DEP_BUF_ADDR
    );
}
