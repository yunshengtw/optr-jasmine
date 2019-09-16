/**
 * main.c
 * Authors: Yun-Sheng Chang
 */
#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "vst.h"
#include "config.h"
#include "vflash.h"
#include "vram.h"
#include "stat.h"
#include "logger.h"
#include "checker.h"

#define N_CRASH 200

static void print_ssd_config(void);
static int load_trace(FILE *fp_trace, struct trace_ent *traces);
static int synthesize_trace(struct trace_ent *traces, int pattern);
static void init(void);
static void cleanup(void);

/* time spent */
time_t begin, end;
double time_spent;

/* parallel testing */
static pid_t *pids;
static int *idxes_child;

/* misc */
int trace_cnt;
static uint64_t raddr, waddr;
static uint32_t rsize, wsize;
static char *fname_img = NULL;
static char *dir_output = NULL;
static int idx_crash = 0;
static int n_success, n_fail;
static int n_jobs;
static int freq_flush = 0;
static uint32_t wid_vst;
static uint32_t wid_latest_flush;

/* unix getopt */
extern char *optarg;
extern int optind;

static int sim_crash;
static int allow_sim_crash;
static struct trace_ent *traces;
static int size_trace;
static void (*vst_open_ftl)(void);
static void (*vst_close_ftl)(void);
static void (*vst_read_sector)(uint32_t, uint32_t);
static void (*vst_write_sector)(uint32_t, uint32_t);
static void (*vst_flush_cache)(void);
static void (*vst_standby)(void);
static uint32_t (*vst_get_epoch_incomplete)(void);
static void (*vst_rwbuf_config)(uint64_t *, uint32_t *, uint64_t *, uint32_t *);

void simulate_crash(void)
{
    pid_t pid;
    char fpath[128];
    FILE *fp_crash;

    if (idx_crash >= N_CRASH || !allow_sim_crash)
        return;
    idx_crash++;

    int idx_pids = 0;
    int status;
    pid_t ret;

    while (pids[idx_pids] != 0) {
        ret = waitpid(pids[idx_pids], &status, WNOHANG);
        if (ret == -1) {
            fprintf(stderr, "Error: waitpid().\n");
            exit(1);
        } else if (ret != 0) {
            /* Child `ret` terminates. */
            if (status == 0)
                n_success++;
            else
                n_fail++;
            pids[idx_pids] = 0;
            fprintf(stderr, "[VST] Validation results: Success: %d. Failure: %d. Total: %d\r", n_success, n_fail, N_CRASH);
        }
        idx_pids = (idx_pids + 1) % n_jobs;
    }

    //fprintf(stderr, "[VST] Crash #%d simulated.\n", idx_crash);
    pid = fork();
    
    if (!pid) {
        /* Recovery and check order-preserving semantics. */
        if (dir_output != NULL) {
            FILE *fp_ret;
            sprintf(fpath, "%s/stdout/stdout-%d.txt", dir_output, idx_crash);
            fp_ret = freopen(fpath, "w", stdout);
            if (fp_ret == NULL) {
                fprintf(stderr, "Fail freopen %s.\n", fpath);
                exit(1);
            }
            sprintf(fpath, "%s/stderr/stderr-%d.txt", dir_output, idx_crash);
            fp_ret = freopen(fpath, "w", stderr);
            if (fp_ret == NULL) {
                fprintf(stderr, "Fail freopen %s.\n", fpath);
                exit(1);
            }
        }
        reset_rwbuf_ptr();
        vst_open_ftl();

        fprintf(stderr, "[VST] Read all sectors.\n");
        for (uint32_t lba = 0; lba < VST_MAX_LBA; lba++) {
            vst_read_sector(lba, 1);
            keep_version(lba);
        }
        uint32_t epoch_incomplete = vst_get_epoch_incomplete();

        int failed_validation = 0;
        fprintf(stderr, "[VST] epoch_incomplete = %u. "
                "Checking order-preserving/flush semantics\n", epoch_incomplete);
        if (!check_prefix(traces, size_trace, epoch_incomplete)) {
            fprintf(stderr, "[VST] Order-preserving semantics IS preserved.\n");
        } else {
            failed_validation = 1;
            fprintf(stderr, "[VST] Order-preserving semantics IS NOT preserved.\n");
        }

        if (epoch_incomplete >= wid_latest_flush) {
            fprintf(stderr, "[VST] Flush semantics IS preserved. wid_incomplete = %u >= wid_latest_flush = %u\n",
                    epoch_incomplete, wid_latest_flush);
        } else {
            failed_validation = 1;
            fprintf(stderr, "[VST] Flush semantics IS NOT preserved. wid_incomplete = %u < wid_latest_flush = %u\n",
                    epoch_incomplete, wid_latest_flush);
        }

        if (failed_validation) {
            if (dir_output == NULL)
                sprintf(fpath, "./crash-%d.img", idx_crash);
            else
                sprintf(fpath, "%s/img/crash-%d.img", dir_output, idx_crash);
            fprintf(stderr, "[VST] Generating crash image to: %s.\n", fpath);
            fp_crash = fopen(fpath, "w");
            serialize_flash(fp_crash);
            fclose(fp_crash);
            exit(1);
        }
        exit(0);
    } else {
        idxes_child[idx_pids] = idx_crash;
        pids[idx_pids] = pid;
    }
}

int main(int argc, char *argv[])
{
    FILE *fp_trace;
    void *handle;
    char *dl_err;
    int opt;
    int one_pass;
    int run_check_prefix;
    int synth_trace;
    uint64_t bound;
    uint32_t lba, sec_num, rw;
    int done;
    char *fname_trace;
    FILE *fp_img;
    int n_wr_between_two_flushes = 0;
    int call_standby;

    begin = clock();

    one_pass = 0;
    run_check_prefix = 0;
    sim_crash = 0;
    synth_trace = 0;
    bound = 1;
    n_jobs = 1;
    call_standby = 0;
    while ((opt = getopt(argc, argv, "ab:cd:f:i:j:pstv")) != -1) {
        switch (opt) {
        case 'a':
            bound = 1099511627776;
            break;
        case 'b':
            bound = atoll(optarg);
            break;
        case 'c':
            one_pass = 1;
            break;
        case 'd':
            dir_output = optarg;
            break;
        case 'f':
            freq_flush = atoi(optarg);
            break;
        case 'i':
            fname_img = optarg;
            break;
        case 'j':
            n_jobs = atoi(optarg);
            break;
        case 'p':
            run_check_prefix = 1;
            break;
        case 's':
            sim_crash = 1;
            break;
        case 't':
            synth_trace = 1;
            break;
        case 'v':
            call_standby = 1;
            break;
        default:
            fprintf(stderr, "Invalid option.\n");
            return 1;
        }
    }

    if (argc <= optind) {
        fprintf(stderr, "usage: ./vst trace_file ftl_obj\n");
        return 1;
    }

    fp_trace = fopen(argv[optind], "r");
    if (fp_trace == NULL) {
        fprintf(stderr, "Fail opening trace file.\n");
        return 1;
    }
    fname_trace = argv[optind];

    handle = dlopen(argv[optind + 1], RTLD_LAZY);
    if (handle == NULL) {
        fprintf(stderr, "Fail opening ftl shared object: %s\n", dlerror());
        return 1;
    }

    vst_open_ftl = (void (*)(void))dlsym(handle, "vst_open_ftl");
    dl_err = dlerror();
    if (dl_err != NULL) {
        fprintf(stderr, "Fail resolving symbol vst_open_ftl.\n");
        return 1;
    }

    int has_close_ftl = 1;
    vst_close_ftl = (void (*)(void))dlsym(handle, "vst_close_ftl");
    dl_err = dlerror();
    if (dl_err != NULL) {
        fprintf(stderr, "No vst_close_ftl provided.\n");
        has_close_ftl = 0;
    }

    vst_read_sector = (void (*)(uint32_t, uint32_t))dlsym(
            handle, 
            "vst_read_sector");
    dl_err = dlerror();
    if (dl_err != NULL) {
        fprintf(stderr, "Fail resolving symbol vst_read_sector.\n");
        return 1;
    }

    vst_write_sector = (void (*)(uint32_t, uint32_t))dlsym(
            handle, 
            "vst_write_sector");
    dl_err = dlerror();
    if (dl_err != NULL) {
        fprintf(stderr, "Fail resolving symbol vst_write_sector.\n");
        return 1;
    }

    vst_flush_cache = (void (*)(void))dlsym(handle, "vst_flush_cache");
    dl_err = dlerror();
    if (dl_err != NULL) {
        fprintf(stderr, "Fail resolving symbol vst_flush_cache.\n");
        return 1;
    }

    int has_standby = 1;
    vst_standby = (void (*)(void))dlsym(handle, "vst_standby");
    dl_err = dlerror();
    if (dl_err != NULL) {
        fprintf(stderr, "No vst_standby provided.\n");
        has_standby = 0;
    }

    if (run_check_prefix || sim_crash) {
        vst_get_epoch_incomplete = (uint32_t (*)(void))dlsym(handle,
                "vst_get_epoch_incomplete");
        dl_err = dlerror();
        if (dl_err != NULL) {
            fprintf(stderr, "Fail resolving symbol vst_get_epoch_incomplete.\n");
            return 1;
        }
    }

    vst_rwbuf_config = (void (*)(uint64_t *, uint32_t *, uint64_t *, uint32_t *))dlsym(handle, "vst_rwbuf_config");
    dl_err = dlerror();
    if (dl_err != NULL) {
        fprintf(stderr, "Fail resolving symbol vst_rwbuf_config.\n");
        return 1;
    }

    vst_rwbuf_config(&raddr, &rsize, &waddr, &wsize);

    init();

    if (fname_img != NULL) {
        fp_img = fopen(fname_img, "r");
        if (fp_img == NULL) {
            fprintf(stderr, "Cannot find the image file.\n");
        } else {
            printf("Flash image: %s\n", fname_img);
            deserialize_flash(fp_img);
            fclose(fp_img);
        }
    }

    pids = calloc(n_jobs, sizeof(pid_t));
    if (pids == NULL) {
        fprintf(stderr, "Fail calloc() `pids`.\n");
        exit(1);
    }

    idxes_child = calloc(n_jobs, sizeof(int));
    if (idxes_child == NULL) {
        fprintf(stderr, "Fail calloc() `idxes_child`.\n");
        exit(1);
    }

    record(LOG_GENERAL, "Trace file: %s\n", fname_trace);

    print_ssd_config();

    atexit(cleanup);

    done = 0;
    traces = (struct trace_ent *)malloc(MAX_SIZE_TRACE * 
            sizeof(struct trace_ent));
    if (synth_trace)
        size_trace = synthesize_trace(traces, 0);
    else
        size_trace = load_trace(fp_trace, traces);

    vst_open_ftl();

    if (run_check_prefix) {
        for (uint32_t lba = 0; lba < VST_MAX_LBA; lba++) {
            vst_read_sector(lba, 1);
            keep_version(lba);
        }
        uint32_t epoch_incomplete = vst_get_epoch_incomplete();
        if (!check_prefix(traces, size_trace, epoch_incomplete))
            printf("Order-preserving semantics IS preserved.\n");
        else
            printf("Order-preserving semantics IS NOT preserved.\n");
        return 0;
    }

    allow_sim_crash = 1;

    while (!done) {
        printf("Trace id = %d\n", trace_cnt);
        fflush(stdout);
        record(LOG_GENERAL, "Trace id = %d\n", trace_cnt);
        for (int i = 0; i < size_trace; i++) {
            lba = traces[i].lba;
            sec_num = traces[i].sec_num;
            rw = traces[i].rw;
            lba += (trace_cnt * 1024); // offset
            if (lba > VST_MAX_LBA) {
                lba %= (VST_MAX_LBA + 1);
                traces[i].lba = lba;
            }
            if (lba + sec_num > VST_MAX_LBA + 1)
                sec_num = VST_MAX_LBA + 1 - lba;
            /* write */
            if (rw == 0) {
                record(LOG_IO, "W: (%u, %u)\n", lba, sec_num);
                send_to_wbuf(lba, sec_num);
                vst_write_sector(lba, sec_num);
                wid_vst++;
                inc_byte_write(sec_num * VST_BYTES_PER_SECTOR);
                if (!one_pass && get_byte_write() > bound) {
                    done = 1;
                    break;
                }
                if (sim_crash && idx_crash == N_CRASH) {
                    done = 1;
                    break;
                }
                n_wr_between_two_flushes++;
                if (n_wr_between_two_flushes == freq_flush) {
                    n_wr_between_two_flushes = 0;
                    vst_flush_cache();
                    wid_latest_flush = wid_vst;
                }
            }
            /* read */
            else {
                record(LOG_IO, "R: (%u, %u)\n", lba, sec_num);
                vst_read_sector(lba, sec_num);
                recv_from_rbuf(lba, sec_num);
                inc_byte_read(sec_num * VST_BYTES_PER_SECTOR);
            }
        }
        if (one_pass)
            done = 1;
        trace_cnt++;
    }

    allow_sim_crash = 0;

    if (has_standby && call_standby)
        vst_standby();

    if (has_close_ftl)
        vst_close_ftl();

    if (sim_crash) {
        int status;
        for (int i = 0; i < n_jobs; i++) {
            if (pids[i] != 0) {
                waitpid(pids[i], &status, 0);
                if (status == 0)
                    n_success++;
                else
                    n_fail++;
                fprintf(stderr, "[VST] Validation results: Success: %d. Failure: %d. Total: %d\r", n_success, n_fail, N_CRASH);
            }
        }
        fprintf(stderr, "\n");
    } else {
        fprintf(stderr, "Pass functional correctness test.\n");
    }

    end = clock();
    time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    record(LOG_GENERAL, "Execution time: %lf (s)\n", time_spent);

    return 0;
}

static void init(void)
{
    open_logger("./vst.log");
    /* open_logger must precede other open_xxx */
    if (open_flash(sim_crash))
        exit(1);
    if (open_ram(raddr, rsize, waddr, wsize))
        exit(1);
    open_stat();
    open_checker();
}

static void cleanup(void)
{
    if (fname_img != NULL) {
        FILE *fp = fopen(fname_img, "w");
        serialize_flash(fp);
        fclose(fp);
    }
    close_flash();
    close_ram();
    close_stat();
    close_checker();
    /* close_logger must succeed other close_xxx */
    close_logger();
}

static void print_ssd_config(void)
{
    printf("----------SSD Configuration----------\n");
    printf("# banks: %d\n", VST_NUM_BANKS);
    printf("# blocks: %d\n", VST_NUM_BLOCKS);
    printf("# pages: %d\n", VST_NUM_PAGES);
    printf("# sectors: %d\n", VST_NUM_SECTORS);
    printf("# blocks per bank: %d\n", VST_BLOCKS_PER_BANK);
    printf("# pages per block: %d\n", VST_PAGES_PER_BLOCK);
    printf("# sectors per page: %d\n", VST_SECTORS_PER_PAGE);
    printf("Max LBA: %d\n", VST_MAX_LBA);
    printf("Sector size: %d\n", VST_BYTES_PER_SECTOR);
    printf("DRAM base: 0x%x\n", VST_DRAM_BASE);
    printf("DRAM size: %d\n", VST_DRAM_SIZE);
    printf("----------SSD Configuration----------\n");
}

/**
 * This function must be called after vst_rwbuf_config() is called to get
 * the valid read write buffer size.
 */
static int load_trace(FILE *fp_trace, struct trace_ent *traces)
{
    char buf[64];
    uint64_t lba;
    uint32_t sec_num, rw;
    int n = 0, n_lines = 0;

    fseek(fp_trace, 0, SEEK_SET);
    while (fscanf(fp_trace, "%*[^,],%*[^,],%*[^,],%[^,],%lu,%u,%*u", 
        buf, &lba, &sec_num) != EOF &&
        n < MAX_SIZE_TRACE) {
        if (!strcmp(buf, "Write"))
            rw = 0;
        else
            rw = 1;
        lba /= 512;
        sec_num /= 512;
        do {
            uint32_t sec_num_this;
            if (rw == 0 && sec_num > wsize)
                sec_num_this = wsize;
            else if (rw == 1 && sec_num > rsize)
                sec_num_this = rsize;
            else
                sec_num_this = sec_num;
            traces[n].lba = lba;
            traces[n].sec_num = sec_num_this;
            traces[n].rw = rw;
            lba += sec_num_this;
            sec_num -= sec_num_this;
            n++;
        } while (sec_num != 0);
        n_lines++;
    }
    fclose(fp_trace);
    printf("Has %u lines. Create %u entries.\n", n_lines, n);
    return n;
}

static int synthesize_trace(struct trace_ent *traces, int pattern)
{
    int n = 0;

    /* sequential access */
    if (pattern == 0) {
        int lpn_max = MAX_LBA / VST_SECTORS_PER_PAGE;
        for (int lpn = 0; lpn < lpn_max; lpn++) {
            traces[n].lba = lpn * VST_SECTORS_PER_PAGE;
            traces[n].sec_num = VST_SECTORS_PER_PAGE;
            traces[n].rw = 0;
            n++;
        }
    }
    printf("Trace synthesis done. Create %u entries.\n", n);
    return n;
}
