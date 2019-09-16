/**
 * cache.c
 * Authors: Yun-Sheng Chang
 */

#include "ftl.h"
#include "blkmgr.h"
#include "pgmap.h"
#include "cache.h"
#include "log.h"
#include "stat.h"

typedef struct {
    UINT8 dirty;
    UINT16 pg_span;
    UINT32 lpn;
} cache_ent_t;

typedef struct {
    cache_ent_t ents[NUM_CACHE_BUFFERS_PER_BANK];
    UINT8 stall;
    UINT16 n_dirty_bufs;
    UINT16 buf_id_incomplete;
    UINT8 lru_list[NUM_CACHE_BUFFERS_PER_BANK];
} cache_t;

static cache_t cache[NUM_BANKS];
static UINT32 pool_bank;
extern UINT32 g_ftl_write_buf_id;
extern UINT32 g_epoch;
extern UINT16 g_pg_span;
extern UINT32 enable_gc_opt;

void init_cache(void)
{
    pool_bank = 0;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        cache[bank].stall = 0;
        cache[bank].n_dirty_bufs = 0;
        cache[bank].buf_id_incomplete = -1;
        for (UINT32 i = 0; i < NUM_CACHE_BUFFERS_PER_BANK; i++) {
            cache[bank].ents[i].dirty = 0;
            cache[bank].ents[i].lpn = -1;
            cache[bank].lru_list[i] = i;
        }
    }
}

void pool_write_buf(void)
{
    if (cache[pool_bank].n_dirty_bufs > NUM_CACHE_BUFFERS_PER_BANK / 2)
        dequeue(pool_bank);
    else
        blkmgr_erase_vt_blk(pool_bank);
    pool_bank = (pool_bank + 1) % NUM_BANKS;
}

void enqueue(UINT32 const bank, UINT32 const lpn, UINT32 const buf_id,
             UINT32 const hole_left, UINT32 const hole_right, UINT32 const comp)
{
    #if EXP_DETAIL
    uart_printf("Eq bk %u lpn %u buf %u\n", bank, lpn, buf_id);
    #endif
    cache_t *cache_p = &cache[bank];
    cache_ent_t *ent_p = &cache_p->ents[buf_id];

    wait_buf_complete(bank, buf_id);

    if (!enable_gc_opt)
        mem_copy(CACHE_BUF(bank, buf_id) + hole_left * BYTES_PER_SECTOR,
                WR_BUF_PTR(g_ftl_write_buf_id) + hole_left * BYTES_PER_SECTOR,
                (SECTORS_PER_PAGE - hole_left - hole_right) * BYTES_PER_SECTOR);
    ent_p->lpn = lpn;
    /* pg_span is a global variable assigned at the beginning of ftl_write() */
    ent_p->pg_span = g_pg_span;
    /* epoch is a global variable increases by 1 on receiving a write request */
    mem_copy(EPOCHS(bank, buf_id), &g_epoch, sizeof(UINT32));
    //ent_p->epoch = g_epoch;
    if (!ent_p->dirty)
        cache_p->n_dirty_bufs++;
    ent_p->dirty = 1;
    if (!comp)
        cache_p->buf_id_incomplete = buf_id;

    UINT16 idx_prev;
    for (idx_prev = 0; cache_p->lru_list[idx_prev] != buf_id; idx_prev++)
        ;
    ASSERT(idx_prev < NUM_CACHE_BUFFERS_PER_BANK);
    for (UINT16 i = idx_prev; i > 0; i--)
        cache_p->lru_list[i] = cache_p->lru_list[i - 1];
    cache_p->lru_list[0] = buf_id;

    if (!enable_gc_opt) {
        g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;

        SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);
        SETREG(BM_STACK_RESET, 0x01);
    }
}

UINT32 dequeue(UINT32 const bank)
{
    if ((GETREG(WR_STAT) & 0x00000001) != 0)
        return 0;
    if (_BSP_FSM(REAL_BANK(bank)) != BANK_IDLE || cache[bank].stall)
        return 0;

    cache_t *cache_p = &cache[bank];

    cache_p->buf_id_incomplete = -1;

    UINT32 idx = 0;
    while (idx < NUM_CACHE_BUFFERS_PER_BANK && !cache_p->ents[idx].dirty)
        idx++;

    /* no dirty entry */
    if (idx == NUM_CACHE_BUFFERS_PER_BANK)
        return 1;

    /* find flushed buffer based on LRU */
    for (UINT32 i = 0; i < NUM_CACHE_BUFFERS_PER_BANK; i++)
        if (cache_p->ents[cache_p->lru_list[i]].dirty)
            idx = cache_p->lru_list[i];

    UINT32 lpn = cache_p->ents[idx].lpn;

    UINT32 old_ppn, new_ppn;
    old_ppn = get_ppn(lpn);
    UINT32 blk, page;
    UINT32 region = 1;
    /* this is an update operation */
    if (old_ppn != 0) {
        blk = old_ppn / PAGES_PER_VBLK;
        page = old_ppn % PAGES_PER_VBLK;
        dec_vcount(bank, blk);
        UINT32 epoch_old;
        mem_copy(&epoch_old, BLK_TIME(bank, blk), sizeof(UINT32));
        UINT32 dist = g_epoch - epoch_old;
        stat_update_distance(dist);
        if (dist < stat_get_dist_median())
            //region = 0;
            region = 1;
    }
    stat_region_balance_factor(bank, region);

    new_ppn = get_and_inc_active_ppn(bank, region);
    blk = new_ppn / PAGES_PER_VBLK;
    page = new_ppn % PAGES_PER_VBLK;

    set_lpn(bank, region, page, lpn);
    set_ppn(lpn, new_ppn);
    inc_vcount(bank, blk);

    /* for checkpointing */
    log_insert_mapent(lpn, new_ppn);

    //mem_copy(HEAD_BUF(bank), CACHE_BUF(bank, idx), BYTES_PER_PAGE);
    cache_p->buf_id_incomplete = idx;

    cache_p->ents[idx].dirty = 0;
    cache_p->n_dirty_bufs--;

    #ifdef VST
    UINT8 spare[64];
    mem_copy(spare, &cache_p->ents[idx].lpn, sizeof(UINT32));
    mem_copy(spare + 4, &cache_p->ents[idx].pg_span, sizeof(UINT16));
    mem_copy(spare + 8, EPOCHS(bank, idx), sizeof(UINT32));
    set_spare(spare, 12);
    #endif

    /* async full-page program */
    if (!enable_gc_opt) {
        stat_data_page();
        nand_page_program(bank, blk, page, CACHE_BUF(bank, idx));
    }

    #if EXP_DETAIL
    uart_printf("Dq bk %u lpn %u buf %u\n", bank, cache_p->ents[idx].lpn, idx);
    #endif

    return 0;
}

void wait_buf_complete(UINT32 const bank, UINT32 const buf_id)
{
    cache_t *cache_p = &cache[bank];

    if (buf_id == cache_p->buf_id_incomplete) {
        while ((GETREG(WR_STAT) & 0x00000001) != 0)
            ;
        while (_BSP_FSM(REAL_BANK(bank)) != BANK_IDLE)
            ;
        cache_p->buf_id_incomplete = -1;
    }
}

void flush_write_buf(void)
{
    UINT32 done;
    do {
        done = 1;
        for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
            done &= dequeue(bank);
        }
    } while (!done);
    flash_finish();
    #if 0
    uart_printf("Write buffer flushed\n");
    #endif
}

UINT32 exist_in_cache(UINT32 const bank, UINT32 const lpn)
{
    for (UINT32 i = 0; i < NUM_CACHE_BUFFERS_PER_BANK; i++)
        if (cache[bank].ents[i].lpn == lpn)
            return i;
    return -1;
}

UINT32 is_cache_ent_dirty(UINT32 const bank, UINT32 const buf_id)
{
    return cache[bank].ents[buf_id].dirty;
}

UINT32 get_cache_ent_epoch(UINT32 const bank, UINT32 const buf_id)
{
    UINT32 epoch;

    mem_copy(&epoch, EPOCHS(bank, buf_id), sizeof(UINT32));
    return epoch;
}

UINT16 get_cache_ent_pg_span(UINT32 const bank, UINT32 const buf_id)
{
    return cache[bank].ents[buf_id].pg_span;
}

UINT32 get_clean_cache_buf(UINT32 const bank)
{
    cache_t *cache_p = &cache[bank];

    UINT32 idx = 0;
    while (cache_p->ents[idx].dirty) {
        pool_write_buf();
        idx = (idx + 1) % NUM_CACHE_BUFFERS_PER_BANK;
    }

    /* find clean buffer based on LRU */
    for (UINT32 i = 0; i < NUM_CACHE_BUFFERS_PER_BANK; i++)
        if (!cache_p->ents[cache_p->lru_list[i]].dirty)
            idx = cache_p->lru_list[i];

    return idx;
}

void stall_cache(UINT32 const bank)
{
    cache[bank].stall = 1;
}

void release_cache(UINT32 const bank)
{
    cache[bank].stall = 0;
}

void cache_collect_dirty_rate(void)
{
    UINT16 n_dirty_bufs[NUM_BANKS];
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        n_dirty_bufs[bank] = cache[bank].n_dirty_bufs;
    stat_dirty_rate(n_dirty_bufs);
}

UINT32 cache_get_total_dirty_bufs(void)
{
    UINT32 n = 0;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        n += cache[bank].n_dirty_bufs;
    return n;
}
