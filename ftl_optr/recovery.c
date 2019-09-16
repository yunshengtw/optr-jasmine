/**
 * recovery.c
 * Authors: Yun-Sheng Chang
 */

#include "ftl.h"
#include "blkmgr.h"
#include "pgmap.h"

static int find_last_commit(void);
static void collect_recovery_entries(void);
static void find_first_incomplete_write(void);
static void pull_epoch_incomplete(void);
static void remap_page_entries(void);
static UINT32 parse_log_pg_type(UINT32 const bank, UINT32 const blk,
                                UINT32 const page);
static void process_commit(UINT32 const bank, UINT32 const blk,
                           UINT32 const page);
static UINT32 reach_last_commit(UINT32 const bank, UINT32 const blk,
                                UINT32 const page);
static void process_mapent(UINT32 const bank, UINT32 const blk,
                           UINT32 const page);
static void retrieve_page_entries(UINT32 const bank, UINT32 const ppn,
                                  UINT32 const mode);
static void process_depent(UINT32 const bank, UINT32 const blk,
                           UINT32 const page);
static void build_depent_list(UINT32 const bank, UINT32 const blk,
                              UINT32 const page);
static void add_recovery_ent(UINT32 const epoch, UINT16 const pg_span);
static void add_depent(UINT32 const epoch_src, UINT32 const epoch_dst,
                       UINT32 const idx);

#define RECOVERY_COMMIT 1
#define RECOVERY_MAPENT 2
#define RECOVERY_DEPENT 3
typedef struct {
    UINT32 epoch_commit;
    UINT32 epoch_max;
    UINT32 epoch_incomplete;
    UINT32 n_depent;
    UINT32 active_ppns[NUM_BANKS][NUM_REGIONS];
} recovery_t;
static recovery_t recovery;

void init_recovery(void)
{
    mem_set_dram(RECOVERY_PAGE_EPOCH_ADDR, 0, RECOVERY_PAGE_EPOCH_BYTES);
}

int analyze(void)
{
    #ifdef VST
    uart_printf("Start analyze phase.\n");
    int done;
    done = find_last_commit();
    if (done) {
        uart_printf("Only the full checkpoint presents; so the recovery ends here.\n");
        return 1;
    }
    collect_recovery_entries();
    find_first_incomplete_write();
    pull_epoch_incomplete();
    uart_printf("Analyze done. Commit: %llu. Max: %llu. Incomplete: %llu.\n",
            recovery.epoch_commit, recovery.epoch_max, recovery.epoch_incomplete);
    return 0;
    #endif
}

void rebuild(void)
{
    #ifdef VST
    uart_printf("Start rebuild phase.\n");
    remap_page_entries();
    uart_printf("Rebuild done.\n");
    #endif
}

UINT32 recovery_get_epoch_incomplete(void)
{
    return recovery.epoch_incomplete;
}

extern UINT32 g_epoch;
static int find_last_commit(void)
{
    UINT32 bank = 0;

    uart_printf("g_epoch = %u.\n", g_epoch);
    recovery.epoch_commit = g_epoch;
    recovery.epoch_max = recovery.epoch_commit;
    recovery.epoch_incomplete = recovery.epoch_commit + 1;
    UINT32 type;
    UINT32 found_at_least_one_commit = 0;
    do {
        UINT32 ppn = get_log_ppn(bank);
        UINT32 blk = ppn / PAGES_PER_VBLK;
        UINT32 pg = ppn % PAGES_PER_VBLK;
        type = parse_log_pg_type(bank, blk, pg);
        switch (type) {
        case RECOVERY_COMMIT:
            found_at_least_one_commit = 1;
            process_commit(bank, blk, pg);
            break;
        case RECOVERY_MAPENT:
            break;
        case RECOVERY_DEPENT:
            break;
        }
        bank = (bank + 1) % NUM_BANKS;
    } while (type);
    if (!found_at_least_one_commit) {
        /** 
         * This is a rare case where the new full checkpoint is written but the
         * first commit page does not present.
         */
        uart_printf("No commit page found.\n");
        return 1;
    } else { 
        uart_printf("Last commit page found. Committed epoch: %llu\n", recovery.epoch_commit);
        return 0;
    }
}

static void collect_recovery_entries(void)
{
    UINT32 bank_log;

    for (bank_log = 0; bank_log < NUM_BANKS; bank_log++)
        revert_log_ppn(bank_log);

    /* retrieve page entries */
    bank_log = 0;
    UINT32 done = 0;
    UINT32 type;
    do {
        UINT32 ppn = get_log_ppn(bank_log);
        UINT32 blk = ppn / PAGES_PER_VBLK;
        UINT32 pg = ppn % PAGES_PER_VBLK;
        type = parse_log_pg_type(bank_log, blk, pg);
        switch (type) {
        case RECOVERY_COMMIT:
            done = reach_last_commit(bank_log, blk, pg);
            break;
        case RECOVERY_MAPENT:
            process_mapent(bank_log, blk, pg);
            break;
        case RECOVERY_DEPENT:
            break;
        default:
            done = 1;
        }
        bank_log = (bank_log + 1) % NUM_BANKS;
    } while (!done);

    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        for (UINT32 region = 0; region < NUM_REGIONS; region++)
            retrieve_page_entries(bank, recovery.active_ppns[bank][region], 0);
        #if 0
        uart_printf("All page entries in bank %u are retrieved.\n", bank);
        #endif
    }

    /* retrieve dependency entries */
    do {
        UINT32 ppn = get_log_ppn(bank_log);
        UINT32 blk = ppn / PAGES_PER_VBLK;
        UINT32 pg = ppn % PAGES_PER_VBLK;
        type = parse_log_pg_type(bank_log, blk, pg);
        switch (type) {
        case RECOVERY_COMMIT:
            break;
        case RECOVERY_MAPENT:
            break;
        case RECOVERY_DEPENT:
            process_depent(bank_log, blk, pg);
            break;
        }
        bank_log = (bank_log + 1) % NUM_BANKS;
    } while (type == RECOVERY_DEPENT);
}

static void find_first_incomplete_write(void)
{
    UINT32 idx = 1;
    while (idx <= recovery.epoch_max - recovery.epoch_commit) {
        UINT32 pg_span = read_dram_32(RECOVERY_ADDR + idx * (2 * sizeof(UINT32)));
        UINT32 cnt = read_dram_32(RECOVERY_ADDR + idx * (2 * sizeof(UINT32)) + 4);
        #if 0
        uart_printf("Epoch %llu: pg = %u found = %u\n", recovery.epoch_commit + idx, pg_span, cnt);
        #endif
        if (!pg_span || pg_span != cnt)
            break;
        idx++;
    }
    recovery.epoch_incomplete = recovery.epoch_commit + idx;
    uart_printf("First incomplete write: %llu.\n", recovery.epoch_incomplete);
}

static void pull_epoch_incomplete(void)
{
    UINT32 bank_log;

    for (bank_log = 0; bank_log < NUM_BANKS; bank_log++)
        revert_log_ppn(bank_log);

    bank_log = 0;
    UINT32 done = 0;
    UINT32 type;
    do {
        UINT32 ppn = get_log_ppn(bank_log);
        UINT32 blk = ppn / PAGES_PER_VBLK;
        UINT32 pg = ppn % PAGES_PER_VBLK;
        type = parse_log_pg_type(bank_log, blk, pg);
        switch (type) {
        case RECOVERY_COMMIT:
            done = reach_last_commit(bank_log, blk, pg);
            break;
        case RECOVERY_MAPENT:
            break;
        case RECOVERY_DEPENT:
            break;
        default:
            done = 1;
        }
        bank_log = (bank_log + 1) % NUM_BANKS;
    } while (!done);

    /* log page after last commit page */
    do {
        UINT32 ppn = get_log_ppn(bank_log);
        UINT32 blk = ppn / PAGES_PER_VBLK;
        UINT32 pg = ppn % PAGES_PER_VBLK;
        type = parse_log_pg_type(bank_log, blk, pg);
        switch (type) {
        case RECOVERY_COMMIT:
            break;
        case RECOVERY_MAPENT:
            break;
        case RECOVERY_DEPENT:
            build_depent_list(bank_log, blk, pg);
            break;
        default:
            done = 1;
        }
        bank_log = (bank_log + 1) % NUM_BANKS;
    } while (type == RECOVERY_DEPENT);

    /* insertion sort */
    for (UINT32 i = 1; i < recovery.n_depent; i++) {
        for (UINT32 j = i; j != 0; j--) {
            UINT32 epoch_src_x, epoch_src_y;
            mem_copy(&epoch_src_x, RECOVERY_ADDR + j * (2 * sizeof(UINT32)), sizeof(UINT32));
            mem_copy(&epoch_src_y, RECOVERY_ADDR + (j - 1) * (2 * sizeof(UINT32)), sizeof(UINT32));
            if (epoch_src_x < epoch_src_y) {
                /* swap j with (j - 1) */
                UINT32 tmp[2];
                mem_copy(
                        tmp,
                        RECOVERY_ADDR + j * (2 * sizeof(UINT32)),
                        2 * sizeof(UINT32)
                );
                mem_copy(
                        RECOVERY_ADDR + j * (2 * sizeof(UINT32)),
                        RECOVERY_ADDR + (j - 1) * (2 * sizeof(UINT32)),
                        2 * sizeof(UINT32)
                );
                mem_copy(
                        RECOVERY_ADDR + (j - 1) * (2 * sizeof(UINT32)),
                        tmp,
                        2 * sizeof(UINT32)
                );
            } else {
                break;
            }
        }
    }

    if (recovery.n_depent > 1) {
        for (UINT32 i = 0; i < recovery.n_depent - 1; i++) {
            UINT32 epoch_src_x, epoch_src_y;
            mem_copy(&epoch_src_x, RECOVERY_ADDR + i * (2 * sizeof(UINT32)), sizeof(UINT32));
            mem_copy(&epoch_src_y, RECOVERY_ADDR + (i + 1) * (2 * sizeof(UINT32)), sizeof(UINT32));
            if (epoch_src_x > epoch_src_y) {
                uart_printf("Depent list not sorted in ascending order.\n");
                while (1)
                    ;
            }
        }
    }
    uart_printf("Total %u dep ents.\n", recovery.n_depent);
    for (UINT32 i = 0; i < recovery.n_depent; i++) {
        UINT32 idx = (recovery.n_depent - i - 1);
        UINT32 epoch_src, epoch_dst;
        mem_copy(&epoch_src, RECOVERY_ADDR + idx * (2 * sizeof(UINT32)), sizeof(UINT32));
        mem_copy(&epoch_dst, RECOVERY_ADDR + idx * (2 * sizeof(UINT32)) + 4, sizeof(UINT32));
        if (epoch_src < recovery.epoch_incomplete && epoch_dst >= recovery.epoch_incomplete) {
            recovery.epoch_incomplete = epoch_src;
        }
    }
}

static void remap_page_entries(void)
{
    UINT32 bank_log;

    for (bank_log = 0; bank_log < NUM_BANKS; bank_log++)
        revert_log_ppn(bank_log);

    /* retrieve page entries */
    bank_log = 0;
    UINT32 done = 0;
    UINT32 type;
    do {
        UINT32 ppn = get_log_ppn(bank_log);
        UINT32 blk = ppn / PAGES_PER_VBLK;
        UINT32 pg = ppn % PAGES_PER_VBLK;
        type = parse_log_pg_type(bank_log, blk, pg);
        switch (type) {
        case RECOVERY_COMMIT:
            done = reach_last_commit(bank_log, blk, pg);
            break;
        case RECOVERY_MAPENT:
            break;
        case RECOVERY_DEPENT:
            break;
        default:
            done = 1;
        }
        bank_log = (bank_log + 1) % NUM_BANKS;
    } while (!done);

    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        for (UINT32 region = 0; region < NUM_REGIONS; region++)
            retrieve_page_entries(bank, recovery.active_ppns[bank][region], 1);
        #if 0
        uart_printf("All page entries in bank %u are retrieved.\n", bank);
        #endif
    }
}

static UINT32 parse_log_pg_type(UINT32 const bank, UINT32 const blk,
                                UINT32 const page)
{
    #if 0
    uart_printf("Parse log pg (%u, %u, %u)\n", bank, blk, page);
    #endif
    UINT32 magic;

    nand_page_ptread(bank, blk, page, 0, SECTORS_PER_PAGE, FTL_BUF(bank), RETURN_WHEN_DONE);
    mem_copy(&magic, FTL_BUF(bank), sizeof(UINT32));
    #if 0
    uart_printf("Magic: %u\n", magic);
    #endif
    switch (magic) {
    case 100:
        #if 0
        uart_printf("Find commit tag page.\n");
        #endif
        return RECOVERY_COMMIT;
    case 200:
        #if 0
        uart_printf("Find mapping entry page.\n");
        #endif
        return RECOVERY_MAPENT;
    case 300:
        #if 0
        uart_printf("Find dependent entry page.\n");
        #endif
        return RECOVERY_DEPENT;
    default:
        #if 0
        uart_printf("Unrecognized magic number.\n");
        #endif
        break;
    }
    return 0;
}

static void process_commit(UINT32 const bank, UINT32 const blk,
                           UINT32 const page)
{
    mem_copy(&recovery.epoch_commit, FTL_BUF(bank) + 4, sizeof(UINT32));
    mem_copy(&recovery.active_ppns, FTL_BUF(bank) + 8, sizeof(recovery.active_ppns));
    recovery.epoch_max = recovery.epoch_commit;
    recovery.epoch_incomplete = recovery.epoch_commit + 1;
    //uart_printf("Commit @ %u\n", recovery.epoch_commit);
}

static UINT32 reach_last_commit(UINT32 const bank, UINT32 const blk,
                                UINT32 const page)
{
    UINT32 epoch;
    mem_copy(&epoch, FTL_BUF(bank) + 4, sizeof(UINT32));
    if (epoch == recovery.epoch_commit) {
        #if 1
        uart_printf("Reach last commit.\n");
        #endif
        return 1;
    }
    return 0;
}

static void process_mapent(UINT32 const bank, UINT32 const blk,
                           UINT32 const page)
{
    UINT32 cnt;
    mem_copy(&cnt, FTL_BUF(bank) + sizeof(UINT32), sizeof(UINT32));
    //uart_printf("# of mapents: %u\n", cnt);

    UINT32 pos = FTL_BUF(bank) + sizeof(UINT32) + sizeof(UINT32);
    UINT32 lpn, ppn;
    for (UINT32 i = 0; i < cnt; i++) {
        mem_copy(&lpn, pos, sizeof(UINT32));
        mem_copy(&ppn, pos + 4, sizeof(UINT32));
        //uart_printf("Map (%u, %u)\n", lpn, ppn);
        set_ppn(lpn, ppn);
        pos += 8;
    }
}

static void retrieve_page_entries(UINT32 const bank, UINT32 const ppn,
                                  UINT32 const mode)
{
    UINT32 done = 0;
    UINT8 spare[64];
    UINT32 blk_cur = ppn / PAGES_PER_VBLK;
    UINT32 pg_cur = ppn % PAGES_PER_VBLK;
    UINT32 lpn;
    UINT16 pg_span;
    UINT32 epoch, epoch_prev;

    while (!done) {
        for (UINT32 pg = pg_cur; pg < PAGES_PER_VBLK - 1; pg++) {
            nand_page_ptread(bank, blk_cur, pg, 0, 1,
                    FTL_BUF(bank), RETURN_WHEN_DONE);
            #ifdef VST
            get_spare(spare, 12);
            mem_copy(&lpn, spare, sizeof(UINT32));
            mem_copy(&pg_span, spare + 4, sizeof(UINT16));
            mem_copy(&epoch, spare + 8, sizeof(UINT32));
            #endif
            if (epoch == (UINT32)(-1))
                break;
            switch (mode) {
            case 0:
                if (epoch == (UINT32)-2) {
                    set_ppn(lpn, blk_cur * PAGES_PER_VBLK + pg);
                }
                else if (epoch > recovery.epoch_commit) {
                    add_recovery_ent(epoch, pg_span);
                }
                break;
            case 1:
                mem_copy(&epoch_prev, RECOVERY_PAGE_EPOCH(lpn), sizeof(UINT32));
                if (epoch < recovery.epoch_incomplete && epoch > epoch_prev) {
                    set_ppn(lpn, blk_cur * PAGES_PER_VBLK + pg);
                    mem_copy(RECOVERY_PAGE_EPOCH(lpn), &epoch, sizeof(UINT32));
                }
                break;
            }
        }

        UINT32 next_blk;
        nand_page_ptread(bank, blk_cur, PAGES_PER_VBLK - 1, 0,
                ROUND_UP(sizeof(UINT32) * PAGES_PER_VBLK + sizeof(UINT32), BYTES_PER_SECTOR) /
                BYTES_PER_SECTOR, FTL_BUF(bank), RETURN_WHEN_DONE);
        mem_copy(&next_blk, FTL_BUF(bank) + PAGES_PER_VBLK * sizeof(UINT32),
                sizeof(UINT32));
        #if 0
        uart_printf("Bank %u next blk is: %u.\n", bank, next_blk);
        #endif

        if (next_blk == (UINT32)(-1)) {
            done = 1;
        } else {
            blk_cur = next_blk;
            pg_cur = 0;
        }
    }
}

static void process_depent(UINT32 const bank, UINT32 const blk,
                           UINT32 const page)
{
    UINT32 cnt;
    mem_copy(&cnt, FTL_BUF(bank) + sizeof(UINT32), sizeof(UINT32));
    //uart_printf("# of depents: %u\n", cnt);

    UINT32 pos = FTL_BUF(bank) + sizeof(UINT32) + sizeof(UINT32);
    UINT32 src, dst;
    UINT16 pg_span;
    for (UINT32 i = 0; i < cnt; i++) {
        mem_copy(&src, pos, sizeof(UINT32));
        mem_copy(&dst, pos + 4, sizeof(UINT32));
        pg_span = read_dram_32(pos + 8);
        #if 0
        uart_printf("Dep (%llu, %llu, %u)\n", src, dst, pg_span);
        #endif
        if (src > recovery.epoch_commit) {
            add_recovery_ent(src, pg_span);
        }
        else
            /* this should not happen */
            uart_printf("Find dependency entries less than committed epoch.\n");

        pos += 12;
    }
}

static void build_depent_list(UINT32 const bank, UINT32 const blk,
                              UINT32 const page)
{
    UINT32 cnt;
    mem_copy(&cnt, FTL_BUF(bank) + sizeof(UINT32), sizeof(UINT32));
    //uart_printf("# of depents: %u\n", cnt);

    UINT32 pos = FTL_BUF(bank) + sizeof(UINT32) + sizeof(UINT32);
    UINT32 src, dst;
    for (UINT32 i = 0; i < cnt; i++) {
        mem_copy(&src, pos, sizeof(UINT32));
        mem_copy(&dst, pos + 4, sizeof(UINT32));
        add_depent(src, dst, recovery.n_depent);
        pos += 12;
        recovery.n_depent++;
    }
}

static void add_recovery_ent(UINT32 const epoch, UINT16 const pg_span)
{
    if (epoch > recovery.epoch_max)
        recovery.epoch_max = epoch;
    UINT32 idx = epoch - recovery.epoch_commit;
    write_dram_32(RECOVERY_ADDR + idx * (2 * sizeof(UINT32)), pg_span);
    UINT32 cnt = read_dram_32(RECOVERY_ADDR + idx * (2 * sizeof(UINT32)) + 4);
    cnt++;
    write_dram_32(RECOVERY_ADDR + idx * (2 * sizeof(UINT32)) + 4, cnt);
    #if 0
    uart_printf("Find epoch %llu (%u, %u).\n", epoch, pg_span, cnt);
    #endif
}

static void add_depent(UINT32 const epoch_src, UINT32 const epoch_dst,
                       UINT32 const idx)
{
    UINT32 base = RECOVERY_ADDR + idx * (2 * sizeof(UINT32));
    mem_copy(base, &epoch_src, sizeof(UINT32));
    mem_copy(base + 4, &epoch_dst, sizeof(UINT32));
}
