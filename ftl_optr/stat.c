/**
 * stat.c
 * Authors: Yun-Sheng Chang
 */

#include "ftl.h"

typedef struct {
    UINT32 usec_flush;
    UINT32 n_flush;
    UINT32 val_para;
    UINT32 n_para;
    UINT32 n_busy[NUM_BANKS];
    UINT32 n_gc[NUM_BANKS];
    UINT32 gc_vcount;
    UINT32 gc_privcount;
    UINT32 n_insert[NUM_BANKS];
    UINT32 n_full_write[NUM_BANKS];
    UINT32 n_update_side[NUM_BANKS];
    UINT32 n_update_center[NUM_BANKS];
    UINT32 n_merge_write[NUM_BANKS];
    UINT32 total_merge;
    UINT32 total_write;
    UINT32 total_insert;
    UINT32 n_chkpt;
    UINT32 gc_degrade;
    UINT32 usec_gc[NUM_BANKS];
    UINT32 gc_erase_sync;
    UINT32 gc_erase_async;
    UINT32 n_dep;
    UINT32 cnt_dep;
    UINT32 n_reclaim;
    UINT32 sects_write;
    UINT32 sects_read;
    UINT32 size_write[15];
    UINT32 size_read[15];
    UINT32 n_dirty_rate;
    UINT32 n_dirty_bufs[NUM_BANKS];
    UINT32 program_region[NUM_BANKS][NUM_REGIONS];
    UINT32 chkpt_page;
    UINT32 tag_page;
    UINT32 dep_page;
    UINT32 chkpt_flush_page;
    UINT32 gc_flush_page;
    UINT32 manual_flush_page;
    UINT32 data_page;
    UINT32 total_gc_blks;
    UINT32 constrained_gc_blks;
} statistic_t;

typedef struct {
    UINT32 total_insert;
    UINT32 total_full_write;
    UINT32 total_update;
    UINT32 total_merge;
    UINT32 total_write;
} write_rate_t;

typedef struct {
    /* update distance will not be reset */
    UINT32 dist_bucket[20];
    UINT32 dist_count;
} distance_t;

static statistic_t stat;
static write_rate_t write_rate;
static distance_t dist;
static UINT32 gtimer_counting;

static UINT32 bucket(UINT32 sects);
static UINT32 get_dist_bucket(UINT32 dist);

void show_stat(void)
{
    uart_printf("# flush: %u Avg time: %lf us\n",
            stat.n_flush,
            (double)stat.usec_flush / stat.n_flush);
    uart_printf("Effec. para: %lf\n",
            (double)stat.val_para / stat.n_para);
    uart_printf("Busy:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%.2lf ",
                (double)stat.n_busy[bank] / stat.n_para);
    uart_printf("\n");
    uart_printf("Insert:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%u ", stat.n_insert[bank]);
    uart_printf("\n");
    uart_printf("Full write:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%u ", stat.n_full_write[bank]);
    uart_printf("\n");
    uart_printf("Update (side):\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%u ", stat.n_update_side[bank]);
    uart_printf("\n");
    uart_printf("Update (center):\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%u ", stat.n_update_center[bank]);
    uart_printf("\n");
    uart_printf("Merged:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%u ", stat.n_merge_write[bank]);
    uart_printf("\n");
    UINT32 total_gc = 0;
    uart_printf("GC:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        uart_printf("%u ", stat.n_gc[bank]);
        total_gc += stat.n_gc[bank];
    }
    uart_printf("Total: %u\n", total_gc);
    uart_printf("GC time:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        if (!stat.n_gc[bank])
            uart_printf("0 ");
        else
            uart_printf("%u ", stat.usec_gc[bank] / stat.n_gc[bank]);
    }
    uart_printf("\n");
    uart_printf("Vcount: %u Avg: %lf\n", stat.gc_vcount, (double)stat.gc_vcount / total_gc);
    uart_printf("Privcount: %u Avg: %lf\n", stat.gc_privcount, (double)stat.gc_privcount / total_gc);
    uart_printf("Degrad.: %u\n", stat.gc_degrade);
    uart_printf("Erase sync: %u async: %u\n", stat.gc_erase_sync, stat.gc_erase_async);
    uart_printf("# chkpt: %u\n", stat.n_chkpt);
    uart_printf("# dep: %u # depent: %u Avg: %lf\n", stat.n_dep, stat.cnt_dep, (double)stat.cnt_dep / stat.n_dep);
    uart_printf("# log reclaiming: %u\n", stat.n_reclaim);
    double tp_write = 0, tp_read = 0;
    #ifndef VST
    if (gtimer_counting) {
        uart_printf("Gtimer ends\n");
        UINT32 time = global_timer_stop();
        gtimer_counting = 0;
        uart_printf("Elapse %u s\n", time);
        tp_write = (double)stat.sects_write * 512 / time / 1000000;
        uart_printf("Write: %lf MB/s\n", tp_write);
        tp_read = (double)stat.sects_read * 512 / time / 1000000;
        uart_printf("Read: %lf MB/s\n", tp_read);
        stat.sects_write = 0;
        stat.sects_read = 0;
    } else {
        uart_printf("Gtimer starts\n");
        global_timer_start();
        gtimer_counting = 1;
    }
    #endif
    uart_printf("Write:\n");
    for (UINT32 i = 0; i < 15; i++)
        uart_printf("%u ", stat.size_write[i]);
    uart_printf("\n");
    uart_printf("Read:\n");
    for (UINT32 i = 0; i < 15; i++)
        uart_printf("%u ", stat.size_read[i]);
    uart_printf("\n");
    uart_printf("Dirty rate:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%.2lf ", (double)stat.n_dirty_bufs[bank] / stat.n_dirty_rate);
    uart_printf("\n");
    uart_printf("H / C:\n");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        uart_printf("%lf ", (double)stat.program_region[bank][0] / stat.program_region[bank][1]);
    uart_printf("\n");
    uart_printf("prefix, %u, %u, %llu, %lf, %u, %lf, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %lf, %lf\n",
        stat.n_flush, stat.total_write, (UINT64)stat.total_insert * BYTES_PER_PAGE,
        (double)stat.total_merge / stat.total_write,
        total_gc,
        (double)stat.gc_vcount / total_gc,
        stat.dep_page,
        stat.chkpt_page, stat.tag_page, stat.chkpt_flush_page,
        stat.n_reclaim * (PAGE_MAP_BYTES / BYTES_PER_PAGE + 1 + 3),
        stat.gc_privcount, stat.gc_flush_page,
        stat.manual_flush_page,
        stat.total_gc_blks, stat.constrained_gc_blks,
        tp_write, tp_read
    );
    uart_printf("optr-page, %u, %u, %u, %u\n",
        stat.dep_page,
        stat.chkpt_page + stat.tag_page,
        stat.n_reclaim * (PAGE_MAP_BYTES / BYTES_PER_PAGE + 1),
        stat.data_page
    );
    uart_printf("Reset stat.\n");

    mem_set_sram(&stat, 0, sizeof(stat));
}

void stat_record_flush(UINT32 t)
{
    stat.usec_flush += t;
    stat.n_flush++;
}

void stat_record_para(UINT32 para)
{
    stat.val_para += para;
    stat.n_para++;
}

void stat_bank_busy(UINT32 bank)
{
    stat.n_busy[bank]++;
}

void stat_record_gc(UINT32 bank, UINT32 t)
{
    stat.usec_gc[bank] += t;
    stat.n_gc[bank]++;
}

void stat_record_insert(UINT32 bank)
{
    stat.n_insert[bank]++;
    stat.total_write++;
    stat.total_insert++;
    write_rate.total_insert++;
    write_rate.total_write++;
}

void stat_record_full_write(UINT32 bank)
{
    stat.n_full_write[bank]++;
    stat.total_write++;
    write_rate.total_full_write++;
    write_rate.total_write++;
}

void stat_record_update_side(UINT32 bank)
{
    stat.n_update_side[bank]++;
    stat.total_write++;
    write_rate.total_update++;
    write_rate.total_write++;
}

void stat_record_update_center(UINT32 bank)
{
    stat.n_update_center[bank]++;
    stat.total_write++;
    write_rate.total_update++;
    write_rate.total_write++;
}

void stat_record_merge_write(UINT32 bank)
{
    stat.n_merge_write[bank]++;
    stat.total_merge++;
    stat.total_write++;
    write_rate.total_merge++;
    write_rate.total_write++;
}

void stat_record_chkpt(void)
{
    stat.n_chkpt++;
}

void stat_gc_vcount(UINT32 vcount)
{
    stat.gc_vcount += vcount;
}

void stat_gc_privcount(UINT32 privcount)
{
    stat.gc_privcount += privcount;
}

void stat_record_gc_degrade(UINT32 degrade)
{
    stat.gc_degrade += degrade;
}

void stat_gc_erase_sync(void)
{
    stat.gc_erase_sync++;
}

void stat_gc_erase_async(void)
{
    stat.gc_erase_async++;
}

void stat_record_dep(UINT32 cnt_dep)
{
    stat.n_dep++;
    stat.cnt_dep += cnt_dep;
}

void stat_reclaim_log(void)
{
    stat.n_reclaim++;
}

void stat_host_write(UINT32 sects)
{
    stat.sects_write += sects;
    #if 1
    UINT32 idx = bucket(sects);
    stat.size_write[idx]++;
    #endif
}

void stat_host_read(UINT32 sects)
{
    stat.sects_read += sects;
    #if 1
    UINT32 idx = bucket(sects);
    stat.size_read[idx]++;
    #endif
}

void stat_dirty_rate(UINT16 *n_dirty_bufs)
{
    stat.n_dirty_rate++;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
        stat.n_dirty_bufs[bank] += n_dirty_bufs[bank];
}

void stat_update_distance(UINT32 distance)
{
    dist.dist_count++;
    UINT32 idx = get_dist_bucket(distance);
    dist.dist_bucket[idx]++;
}

void stat_chkpt_page(void)
{
    stat.chkpt_page++;
}

void stat_tag_page(void)
{
    stat.tag_page++;
}

void stat_dep_page(void)
{
    stat.dep_page++;
}

void stat_data_page(void)
{
    stat.data_page++;
}

void stat_chkpt_flush_page(UINT32 n_pg)
{
    stat.chkpt_flush_page += n_pg;
}

void stat_gc_flush_page(UINT32 n_pg)
{
    stat.gc_flush_page += n_pg;
}

void stat_manual_flush_page(UINT32 n_pg)
{
    stat.manual_flush_page += n_pg;
}

void stat_total_gc_blks(UINT32 n_blks)
{
    stat.total_gc_blks += n_blks;
}

void stat_constrained_gc_blks(UINT32 n_blks)
{
    stat.constrained_gc_blks += n_blks;
}

#define PERIOD_SHOW_STAT_SEC    300
static UINT32 period_prev;
void stat_periodic_show_stat(void)
{
    UINT32 rtime;
    UINT32 period;
    if (gtimer_counting) {
        rtime = 0xFFFFFFFF - GET_TIMER_VALUE(TIMER_CH3);
        // Tick to sec
        rtime = (UINT32)((UINT64)rtime * 2 * PRESCALE_TO_DIV(TIMER_PRESCALE_2) / CLOCK_SPEED);
        period = rtime / PERIOD_SHOW_STAT_SEC;
        if (period != period_prev) {
            period_prev = period;
            UINT32 total_gc = 0;
            for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
                total_gc += stat.n_gc[bank];
            uart_printf("[%u] %.2lf %.2lf %.2lf %.2lf avg vcount %lf\n", rtime,
                    (double)write_rate.total_insert / write_rate.total_write,
                    (double)write_rate.total_full_write / write_rate.total_write,
                    (double)write_rate.total_update / write_rate.total_write,
                    (double)write_rate.total_merge / write_rate.total_write,
                    (double)stat.gc_vcount / total_gc
            );
            mem_set_sram(&write_rate, 0, sizeof(write_rate));
        }
    }
}

#define REFRESH_DIST_MEDIAN 10000
static UINT32 dist_median_record;
static UINT32 called_dist_median;
UINT32 stat_get_dist_median(void)
{
    called_dist_median++;
    if (called_dist_median > REFRESH_DIST_MEDIAN) {
        called_dist_median = 0;
        UINT32 idx_median = dist.dist_count / 20 * 19;
        UINT32 acc = 0;
        for (UINT32 bucket = 0; bucket < 20; bucket++) {
            acc += dist.dist_bucket[bucket];
            if (acc > idx_median) {
                dist_median_record = 1 << (bucket + 2);
                return dist_median_record;
            }
        }
        //mem_set_sram(&dist, 0, sizeof(dist));
    }
    return dist_median_record;
}

void stat_region_balance_factor(UINT32 bank, UINT32 region)
{
    stat.program_region[bank][region]++;
}

static UINT32 bucket(UINT32 sects)
{
    /**
     * idx      range
     * 0        [0, 4)
     * 1        [4, 8)
     * 2        [8, 16)
     * 3        [16, 32)
     * 4        [32, 64)
     * 5        [64, 128)
     * 6        [128, 256)
     * 7        [256, 512)
     * 8        [512, 1024)
     * 9        [1024, 2048)
     * 10       [2048, 2048)
     * 11       [4096, 8192)
     * 12       [8192, 16384)
     * 13       [16384, 32768)
     * 14       [32768, inf)
     */
    UINT32 idx;
    sects /= 4;
    for (idx = 0; idx < 15; idx++) {
        if (sects == 0)
            return idx;
        sects /= 2;
    }
    return 14;
}

static UINT32 get_dist_bucket(UINT32 dist)
{
    /**
     * idx      range
     * 0        [0, 4)
     * 1        [4, 8)
     * 2        [8, 16)
     * 3        [16, 32)
     * 4        [32, 64)
     * 5        [64, 128)
     * 6        [128, 256)
     * 7        [256, 512)
     * 8        [512, 1024)
     * 9        [1024, 2048)
     * 10       [2048, 2048)
     * 11       [4096, 8192)
     * 12       [8192, 16384)
     * 13       [16384, 32768)
     * 14       [32768, 65536)
     * 15       [65536, 131072)
     * 16       [131072, 262144)
     * 17       [262144, 524288)
     * 18       [524288, 1048576)
     * 19       [1048576, inf)
     */
    UINT32 idx;
    dist /= 4;
    for (idx = 0; idx < 19; idx++) {
        if (dist == 0)
            return idx;
        dist /= 2;
    }
    return 19;
}
