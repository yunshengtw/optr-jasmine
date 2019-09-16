/**
 * vram.c
 * Authors: Yun-Sheng Chang
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "config.h"
#include "logger.h"
#include "vram.h"
#include "vpage.h"
#include "checker.h"

static void replay_to_commit(struct trace_ent *traces, int size_trace,
                             uint32_t epoch_incomplete);

typedef struct {
    vpage_t *pages;
    uint32_t size;
    uint32_t ptr;
} rw_buf_t;

typedef struct {
    vpage_t pages[VST_DRAM_SIZE / VST_BYTES_PER_PAGE];
} ram_t;

/* emulated DRAM */
uint8_t __attribute__((section (".dram"))) dram[VST_DRAM_SIZE];
static ram_t vram;
static rw_buf_t rbuf, wbuf;
static uint32_t *vers;
static uint32_t *vers_rec;

/* RAM APIs */
uint8_t vst_read_dram_8(uint64_t addr)
{
    return *(uint8_t *)addr;
}

uint16_t vst_read_dram_16(uint64_t addr)
{
    assert(!(addr & 1));

    return *(uint16_t *)addr;
}

uint32_t vst_read_dram_32(uint64_t addr)
{
    assert(!(addr & 3));

    return *(uint32_t *)addr;
}

void vst_write_dram_8(uint64_t addr, uint8_t val)
{
    vpage_t *pp = vram_vpage_map(addr);
    untag_page(pp);
    *(uint8_t *)addr = val;
}

void vst_write_dram_16(uint64_t addr, uint16_t val)
{
    assert(!(addr & 1));

    vpage_t *pp = vram_vpage_map(addr);
    untag_page(pp);
    *(uint16_t *)addr = val;
}

void vst_write_dram_32(uint64_t addr, uint32_t val)
{
    assert(!(addr & 3));

    vpage_t *pp = vram_vpage_map(addr);
    untag_page(pp);
    *(uint32_t *)addr = val;
}

void vst_set_bit_dram(uint64_t base_addr, uint32_t bit_offset)
{
    uint64_t addr = base_addr + bit_offset / 8;
    uint32_t offset = bit_offset % 8;

    vpage_t *pp = vram_vpage_map(base_addr);
    untag_page(pp);
    *(uint8_t *)addr = *(uint8_t *)addr | (1 << offset);
}

void vst_clr_bit_dram(uint64_t base_addr, uint32_t bit_offset)
{
    uint64_t addr = base_addr + bit_offset / 8;
    uint32_t offset = bit_offset % 8;

    vpage_t *pp = vram_vpage_map(base_addr);
    untag_page(pp);
    *(uint8_t *)addr = *(uint8_t *)addr & ~(1 << offset);
}

uint32_t vst_tst_bit_dram(uint64_t base_addr, uint32_t bit_offset)
{
    uint64_t addr = base_addr + bit_offset / 8;
    uint32_t offset = bit_offset % 8;

    return (*(uint8_t *)addr) & (1 << offset);
}

void vst_memcpy(uint64_t dst, uint64_t src, uint32_t len)
{
    record(LOG_RAM, "memcpy: mem[0x%lx] -> mem[0x%lx] of len %u\n", src, dst, len);

    vpage_t *pp_dst, *pp_src;
    pp_dst = vram_vpage_map(dst);
    pp_src = vram_vpage_map(src);
    if (pp_src == NULL) {
        /* sram -> sram/dram */
        if (pp_dst != NULL) {
            /* sram -> dram */
            vpage_t *pp_dst_end;
            pp_dst_end = vram_vpage_map(dst + len);
            for (vpage_t *pp = pp_dst; pp <= pp_dst_end; pp++)
                untag_page(pp);
        }
        memcpy((void *)dst, (void *)src, len);
    } else {
        /* dram -> sram/dram */
        if (pp_dst == NULL) {
            /* dram -> sram */
            if (!pp_src->tagged)
                memcpy((void *)dst, (void *)src, len);
            else
                /* there's nothing we can do if dram is tagged */
                record(LOG_RAM, "Try to move tagged DRAM data to SRAM\n");
        } else {
            /* dram -> dram */
            vpage_t *pp_dst_end;
            pp_dst_end = vram_vpage_map(dst + len);
            if (!pp_src->tagged) {
                for (vpage_t *pp = pp_dst; pp <= pp_dst_end; pp++)
                    untag_page(pp);
                memcpy((void *)dst, (void *)src, len);
            } else {
                for (vpage_t *pp = pp_dst; pp <= pp_dst_end; pp++)
                    tag_page(pp);
                record(LOG_RAM, "Tagged data movement\n");
                /* only support sector-aligned tagged data copy */
                if (dst % VST_BYTES_PER_SECTOR != 0 ||
                        src % VST_BYTES_PER_SECTOR != 0 ||
                        len % VST_BYTES_PER_SECTOR != 0) {
                    abort();
                }
                int x, y, n_sect;
                x = dst % VST_BYTES_PER_PAGE / VST_BYTES_PER_SECTOR;
                y = src % VST_BYTES_PER_PAGE / VST_BYTES_PER_SECTOR;
                n_sect = len / VST_BYTES_PER_SECTOR;
                for (int i = 0; i < n_sect; i++) {
                    record(LOG_RAM, "\tmem[%p] + sec[%d] -> mem[%p] + sec[%d], lba = %u\n",
                            pp_src->data, y, pp_dst->data, x, pp_src->sects[y].lba);
                    pp_dst->sects[x] = pp_src->sects[y];
                    x++;
                    y++;
                    if (x == VST_SECTORS_PER_PAGE) {
                        x = 0;
                        pp_dst++;
                    }
                    if (y == VST_SECTORS_PER_PAGE) {
                        y = 0;
                        pp_src++;
                    }
                }
            }
        }
    }
}

void vst_memset(uint64_t addr, uint32_t val, uint32_t len)
{
    record(LOG_RAM, "memset: mem[0x%lx] of len %u\n", addr, len);

    vpage_t *pp_tgt;
    pp_tgt = vram_vpage_map(addr);
    if (pp_tgt != NULL) {
        vpage_t *pp_tgt_end;
        pp_tgt_end = vram_vpage_map(addr + len);
        assert(pp_tgt_end);
        for (vpage_t *pp = pp_tgt; pp <= pp_tgt_end; pp++)
            untag_page(pp);
    }
    memset((void *)addr, val, len);
}

uint32_t vst_mem_search_min(uint64_t addr, uint32_t unit, uint32_t size)
{
    assert(unit == 1 || unit == 2 || unit == 4);
    assert(!(addr % unit));
    assert(size != 0);

    uint32_t i;
    uint32_t idx = 0;
    if (unit == 1) {
        uint8_t *vals = (uint8_t *)addr;
        uint8_t min = vals[0];
        if (min == 0)
            return idx;
        for (i = 1; i < size; i++) {
            if (vals[i] < min) {
                min = vals[i];
                idx = i;
                if (min == 0)
                    return idx;
            }
        }
    } else if (unit == 2) {
        uint16_t *vals = (uint16_t *)addr;
        uint16_t min = vals[0];
        if (min == 0)
            return idx;
        for (i = 1; i < size; i++) {
            if (vals[i] < min) {
                min = vals[i];
                idx = i;
                if (min == 0)
                    return idx;
            }
        }
    } else {
        uint32_t *vals = (uint32_t *)addr;
        uint32_t min = vals[0];
        if (min == 0)
            return idx;
        for (i = 1; i < size; i++) {
            if (vals[i] < min) {
                min = vals[i];
                idx = i;
                if (min == 0)
                    return idx;
            }
        }
    }
    return idx;
}

uint32_t vst_mem_search_max(uint64_t addr, uint32_t unit, uint32_t size)
{
    assert(!(addr % size));
    assert(unit == 1 || unit == 2 || unit == 4);
    assert(size != 0);

    uint32_t i;
    uint32_t idx = 0;
    if (unit == 1) {
        uint8_t *vals = (uint8_t *)addr;
        uint8_t max = vals[0];
        for (i = 1; i < size; i++) {
            if (vals[i] > max) {
                max = vals[i];
                idx = i;
            }
        }
    } else if (unit == 2) {
        uint16_t *vals = (uint16_t *)addr;
        uint16_t max = vals[0];
        for (i = 1; i < size; i++) {
            if (vals[i] > max) {
                max = vals[i];
                idx = i;
            }
        }
    } else {
        uint32_t *vals = (uint32_t *)addr;
        uint32_t max = vals[0];
        for (i = 1; i < size; i++) {
            if (vals[i] > max) {
                max = vals[i];
                idx = i;
            }
        }
    }
    return idx;
}

uint32_t vst_mem_search_equ(uint64_t addr, uint32_t unit,
                            uint32_t size, uint32_t val)
{
    //TODO
    return 0;
}

uint32_t vst_get_rbuf_ptr(void)
{
    return rbuf.ptr;
}

uint32_t vst_get_wbuf_ptr(void)
{
    return wbuf.ptr;
}

int open_ram(uint64_t raddr, uint32_t rsize, uint64_t waddr, uint32_t wsize)
{
    memset(dram, 0, VST_DRAM_SIZE);

    for (int i = 0; i < VST_DRAM_SIZE / VST_BYTES_PER_PAGE; i++) {
        vram.pages[i].tagged = 0;
        vram.pages[i].data =
                (uint8_t *)(uint64_t)(VST_DRAM_BASE + i * VST_BYTES_PER_PAGE);
    }
    record(LOG_RAM, "DRAM @ %x of size %u B\n", VST_DRAM_BASE, VST_DRAM_SIZE);

    assert(raddr >= VST_DRAM_BASE && raddr < VST_DRAM_BASE + VST_DRAM_SIZE);
    assert((raddr - VST_DRAM_BASE) % VST_BYTES_PER_PAGE == 0);
    rbuf.pages = &vram.pages[(raddr - VST_DRAM_BASE) / VST_BYTES_PER_PAGE];
    rbuf.size = rsize;
    rbuf.ptr = 0;
    record(LOG_RAM, "Read buffer @ %lx of size %u\n", raddr, rsize);

    assert(waddr >= VST_DRAM_BASE && waddr < VST_DRAM_BASE + VST_DRAM_SIZE);
    assert((waddr - VST_DRAM_BASE) % VST_BYTES_PER_PAGE == 0);
    wbuf.pages = &vram.pages[(waddr - VST_DRAM_BASE) / VST_BYTES_PER_PAGE];
    wbuf.size = wsize;
    wbuf.ptr = 0;
    record(LOG_RAM, "Write buffer @ %lx of size %u\n", waddr, wsize);

    vers = calloc(VST_MAX_LBA, sizeof(uint32_t));
    vers_rec = calloc(VST_MAX_LBA, sizeof(uint32_t));
    if (vers == NULL || vers_rec == NULL) {
        fprintf(stderr, "Fail allocating memory for vers and vers_rec.\n");
        return 1;
    }

    record(LOG_RAM, "Virtual RAM initialized\n");
    return 0;
}

void close_ram(void)
{
}

void reset_rwbuf_ptr(void)
{
    rbuf.ptr = 0;
    wbuf.ptr = 0;
    memset(dram, 0, VST_DRAM_SIZE);
}

void send_to_wbuf(uint32_t lba, uint32_t n_sect)
{
    uint32_t l, r, m, s;

    l = lba;
    r = n_sect;
    s = lba % VST_SECTORS_PER_PAGE;
    while (r > 0) {
        if (s + r < VST_SECTORS_PER_PAGE)
            m = r;
        else
            m = VST_SECTORS_PER_PAGE - s;

        tag_page(&wbuf.pages[wbuf.ptr]);
        for (uint32_t i = 0; i < m; i++) {
            /* fill in lba */
            vers[l + i]++;
            wbuf.pages[wbuf.ptr].sects[s + i].lba = l + i;
            wbuf.pages[wbuf.ptr].sects[s + i].ver = vers[l + i];
        }
        wbuf.ptr = (wbuf.ptr + 1) % wbuf.size;

        l += m;
        s = 0;
        r -= m;
    }
}

void recv_from_rbuf(uint32_t lba, uint32_t n_sect)
{
    uint32_t l, r, m, s;

    l = lba;
    r = n_sect;
    s = lba % VST_SECTORS_PER_PAGE;
    while (r > 0) {
        if (s + r < VST_SECTORS_PER_PAGE)
            m = r;
        else
            m = VST_SECTORS_PER_PAGE - s;

        chk_lpn_consistent(&rbuf.pages[rbuf.ptr], l, s, m, vers);
        //printf("vst: %u\n", rbuf.ptr);
        rbuf.ptr = (rbuf.ptr + 1) % rbuf.size;

        l += m;
        s = 0;
        r -= m;
    }
}

void keep_version(uint32_t lba)
{
    uint32_t lba_rec, ver_rec;
    lba_rec = rbuf.pages[rbuf.ptr].sects[lba % VST_SECTORS_PER_PAGE].lba;
    ver_rec = rbuf.pages[rbuf.ptr].sects[lba % VST_SECTORS_PER_PAGE].ver;
    if (!rbuf.pages[rbuf.ptr].tagged || lba != lba_rec)
        ver_rec = 0;
    vers_rec[lba] = ver_rec;
    rbuf.ptr = (rbuf.ptr + 1) % rbuf.size;
}

int check_prefix(struct trace_ent *traces, int size_trace, uint32_t epoch_incomplete)
{
    record(LOG_RECOVERY, "Start checking prefix semantics.\n");
    replay_to_commit(traces, size_trace, epoch_incomplete);

    int ret = 0;
    for (int i = 0; i < VST_MAX_LBA; i++) {
        if (vers_rec[i] != vers[i]) {
            record(LOG_RECOVERY, "[recovery = %u, golden = %u] @ lba %d.\n",
                    vers_rec[i], vers[i], i);
            ret = 1;
        }
    }
    record(LOG_RECOVERY, "Done checking prefix semantics.\n");
    return ret;
}

void dump_version(uint32_t lba, FILE *fp)
{
    uint32_t lba_rec, ver_rec;
    lba_rec = rbuf.pages[rbuf.ptr].sects[lba % VST_SECTORS_PER_PAGE].lba;
    ver_rec = rbuf.pages[rbuf.ptr].sects[lba % VST_SECTORS_PER_PAGE].ver;
    if (!rbuf.pages[rbuf.ptr].tagged || lba != lba_rec)
        ver_rec = 0;
    fprintf(fp, "%u\n", ver_rec);
    rbuf.ptr = (rbuf.ptr + 1) % rbuf.size;
}

void serialize_version(char *fname)
{
    FILE *fp = fopen(fname, "w");
    if (fp == NULL) {
        fprintf(stderr, "Fail opening version file: %s\n", fname);
        return;
    }
    for (uint32_t lba = 0; lba < VST_MAX_LBA; lba++)
        fprintf(fp, "%u\n", vers[lba]);
    fclose(fp);
}

vpage_t *vram_vpage_map(uint64_t dram_addr)
{
    if (dram_addr >= VST_DRAM_BASE &&
        dram_addr < VST_DRAM_BASE + VST_DRAM_SIZE) {
        return &vram.pages[(dram_addr - VST_DRAM_BASE) / VST_BYTES_PER_PAGE];
    }
    return NULL;
}

static void replay_to_commit(struct trace_ent *traces, int size_trace,
                             uint32_t epoch_incomplete)
{
    uint32_t lba, sec_num, rw;
    uint32_t epoch = 0;
    int trace_cnt = 0;
    memset(vers, 0, VST_MAX_LBA * sizeof(uint32_t));
    while (1) {
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
            if (rw == 0) {
                for (uint32_t offset = 0; offset < sec_num; offset++)
                    vers[lba + offset]++;
                epoch++;
                if (epoch == epoch_incomplete)
                    return;
            }
        }
        trace_cnt++;
    }
}
