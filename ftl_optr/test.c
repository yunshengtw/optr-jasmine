/**
 * test.c
 * Author: Yun-Sheng Chang
 */
#include <stdlib.h>
#include "ftl.h"
#include "stat.h"
#include "blkmgr.h"

/**
 * Footprint: sector
 * Flush interval: request
 * Granularity: sector
 */
#define SECTOR_16KB         (16 * 2)
#define SECTOR_1MB          (1  * 1024 * 2)
#define SECTOR_8MB          (8  * 1024 * 2)
#define SECTOR_16MB         (16 * 1024 * 2)
#define SECTOR_80MB         (80 * 1024 * 2)
#define SECTOR_1GB          (1  * 1024 * 1024 * 2)
#define SECTOR_2GB          (2  * 1024 * 1024 * 2)
#define SECTOR_4GB          (4  * 1024 * 1024 * 2)
#define SECTOR_16GB         (16 * 1024 * 1024 * 2)

#define GRAN_SMALL          0
#define GRAN_LARGE          1
#define GRAN_HYBRID         2

extern UINT32 enable_gc_opt;
static inline UINT32 get_lba(UINT32 footprint, UINT32 gran)
{
    UINT32 lba;

    lba = (rand() % footprint) / gran * gran;
    return lba;
}

static void atc_exp(UINT32 footprint, UINT32 gran, UINT32 flush_interval)
{
    UINT32 total_write = 0;
    UINT32 cnt_write = 0;
    UINT32 lba;

    srand(815);
    if (footprint > MAX_LBA) {
        uart_printf("footprint cannot be larger than MAX_LBA. Halt.\n");
        while (1)
            ;
    }
    ftl_open();
    uart_printf("[exp-log] footprint = %u (sectors), gran mode = %u, flush_interval = %u (writes)\n",
        footprint, gran, flush_interval
    );

#if 0
    enable_gc_opt = 1;
    while (!blkmgr_first_gc_triggered()) {
        lba = get_lba(SECTOR_1GB, SECTOR_8MB);
        ftl_write(lba, SECTOR_8MB);
        total_write += SECTOR_8MB;
        if (total_write % SECTOR_1GB == 0)
            uart_printf("%u GB data prepared\n", total_write / SECTOR_1GB);
    }
    for (UINT32 i = 0; i < SECTOR_2GB / SECTOR_8MB; i++) {
        lba = get_lba(SECTOR_1GB, SECTOR_8MB);
        ftl_write(lba, SECTOR_8MB);
    }
    enable_gc_opt = 0;
#endif
    ftl_idle();

    show_stat();
    uart_printf("Disable gc opt\n");

    total_write = 0;
    while (total_write < SECTOR_4GB) {
        UINT32 amnt;
        if (gran == 0)
            amnt = SECTOR_16KB;
        else if (gran == 1)
            amnt = SECTOR_1MB;
        else
            amnt = (rand() % 2) ? SECTOR_16KB : SECTOR_1MB;
        lba = get_lba(footprint, amnt);
        ftl_write(lba, amnt);
        total_write += amnt;
        cnt_write++;
        if (cnt_write == flush_interval) {
            cnt_write = 0;
            ftl_flush();
        }
        if (total_write % SECTOR_1GB == 0)
            uart_printf("%u GB data written\n", total_write / SECTOR_1GB);
    }
    show_stat();
}

static void synth_line(UINT32 footprint, UINT32 gran)
{
    for (UINT32 exp = 0; exp < 8; exp++) {
        UINT32 flush_interval = 1;
        for (UINT32 e = 0; e < exp; e++)
            flush_interval *= 4;
        atc_exp(footprint, gran, flush_interval);
    }
}

void ftl_test(void)
{
    synth_line(SECTOR_1MB,  GRAN_SMALL);
    synth_line(SECTOR_80MB, GRAN_SMALL);
    synth_line(SECTOR_1GB,  GRAN_SMALL);

    synth_line(SECTOR_1MB,  GRAN_LARGE);
    synth_line(SECTOR_80MB, GRAN_LARGE);
    synth_line(SECTOR_1GB,  GRAN_LARGE);

    synth_line(SECTOR_1MB,  GRAN_HYBRID);
    synth_line(SECTOR_80MB, GRAN_HYBRID);
    synth_line(SECTOR_1GB,  GRAN_HYBRID);
}
