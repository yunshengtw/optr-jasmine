/**
 * port.c
 * Authors: Yun-Sheng Chang
 */

#include <string.h>
#include "ftl.h"
#include "vst-api.h"

/* VST tags */
static UINT8 omit = 0;
static UINT8 spare[64];

/* FTL metadata */
extern UINT32 g_ftl_read_buf_id;
extern UINT32 g_ftl_write_buf_id;

/* VST tag operations */
void omit_next_dram_op(void)
{
    omit = 1;
}

/* host operations */
void vst_open_ftl(void)
{
    ftl_open();
}

void vst_close_ftl(void)
{
    ftl_close();
}

void vst_read_sector(uint32_t lba, uint32_t n_sect)
{
    ftl_read(lba, n_sect);
}

void vst_write_sector(uint32_t lba, uint32_t n_sect)
{
    ftl_write(lba, n_sect);
}

void vst_flush_cache(void)
{
    ftl_flush();
}

void vst_standby(void)
{
    ftl_standby();
}

uint32_t vst_get_epoch_incomplete(void)
{
    return ftl_get_epoch_incomplete();
}

void vst_recovery(void)
{
    ftl_recovery();
}

void vst_rwbuf_config(uint64_t *raddr, uint32_t *rsize, uint64_t *waddr, uint32_t *wsize)
{
    *raddr = RD_BUF_ADDR;
    *rsize = NUM_RD_BUFFERS;
    *waddr = WR_BUF_ADDR;
    *wsize = NUM_WR_BUFFERS;
}

uint32_t vst_get_epoch(void)
{
    return ftl_get_epoch();
}

/* flash wrappers */
void nand_page_read(UINT32 const bank, UINT32 const vblock, 
                    UINT32 const page_num, UINT32 const buf_addr)
{
    vst_read_page(bank, vblock, page_num, 0, SECTORS_PER_PAGE,
            (UINT64)buf_addr, spare);
}

void nand_page_ptread(UINT32 const bank, UINT32 const vblock, 
                      UINT32 const page_num, UINT32 const sect_offset, 
                      UINT32 const num_sectors, UINT32 const buf_addr, 
                      UINT32 const issue_flag)
{
    vst_read_page(bank, vblock, page_num, sect_offset, num_sectors,
                  (UINT64)buf_addr, spare);
}

void nand_page_read_to_host(UINT32 const bank, UINT32 const vblock,
                           UINT32 const page_num)
{
    vst_read_page(bank, vblock, page_num, 0, SECTORS_PER_PAGE,
                  (UINT64)RD_BUF_PTR(g_ftl_read_buf_id), NULL);
    g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
}

void nand_page_ptread_to_host(UINT32 const bank, UINT32 const vblock, 
                              UINT32 const page_num, UINT32 const sect_offset,
                              UINT32 const num_sectors)
{
    vst_read_page(bank, vblock, page_num, sect_offset, num_sectors, 
                  (UINT64)RD_BUF_PTR(g_ftl_read_buf_id), NULL); 
    g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
}

void nand_page_program(UINT32 const bank, UINT32 const vblock, 
                       UINT32 const page_num, UINT32 const buf_addr)
{
    vst_write_page(bank, vblock, page_num, 0, SECTORS_PER_PAGE,
                   (UINT64)buf_addr, spare);
}

void nand_page_ptprogram(UINT32 const bank, UINT32 const vblock, 
                         UINT32 const page_num, UINT32 const sect_offset, 
                         UINT32 const num_sectors, UINT32 const buf_addr)
{
    vst_write_page(bank, vblock, page_num, sect_offset, num_sectors,
                   (UINT64)buf_addr, spare);
}

void nand_page_ptprogram_sync(UINT32 const bank, UINT32 const vblock, 
                         UINT32 const page_num, UINT32 const sect_offset, 
                         UINT32 const num_sectors, UINT32 const buf_addr)
{
    vst_write_page(bank, vblock, page_num, sect_offset, num_sectors,
                   (UINT64)buf_addr, spare);
}

void nand_page_program_from_host(UINT32 const bank, UINT32 const vblock, 
                                 UINT32 const page_num)
{
    vst_write_page(bank, vblock, page_num, 0, SECTORS_PER_PAGE,
                   (UINT64)WR_BUF_PTR(g_ftl_write_buf_id), spare); 
    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
}

void nand_page_ptprogram_from_host(UINT32 const bank, UINT32 const vblock, 
                                   UINT32 const page_num, 
                                   UINT32 const sect_offset, 
                                   UINT32 const num_sectors)
{
    vst_write_page(bank, vblock, page_num, sect_offset, num_sectors,
                   (UINT64)WR_BUF_PTR(g_ftl_write_buf_id), spare);
    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
}

void nand_page_copyback(UINT32 const bank,
                        UINT32 const src_vblock, UINT32 const src_page,
                        UINT32 const dst_vblock, UINT32 const dst_page)
{
    vst_copyback_page(bank, src_vblock, src_page, 
                      dst_vblock, dst_page, spare);
}

void nand_block_erase(UINT32 const bank, UINT32 const vblock)
{
    vst_erase_block(bank, vblock);
}

void nand_block_erase_sync(UINT32 const bank, UINT32 const vblock)
{
    vst_erase_block(bank, vblock);
}

void set_spare(void *spare_src, UINT32 const size)
{
    memcpy(spare, spare_src, size);
}

void get_spare(void *spare_dst, UINT32 const size)
{
    memcpy(spare_dst, spare, size);
}

void _mem_copy(const UINT64 dst, const UINT64 src, UINT32 const bytes)
{
    if (omit) {
        omit = 0;
        vpage_t *pp_start = vram_vpage_map(dst);
        vpage_t *pp_end = vram_vpage_map(dst + bytes - 1);
        for (vpage_t *pp = pp_start; pp <= pp_end; pp++)
            untag_page(pp);
        return;
    }
    vst_memcpy(dst, src, bytes);
}

UINT32 _mem_search_min_max(UINT64 const addr, UINT32 const unit, UINT32 const size, UINT32 const cmd)
{
    if (cmd == MU_CMD_SEARCH_MIN_DRAM || cmd == MU_CMD_SEARCH_MIN_SRAM)
        return vst_mem_search_min(addr, unit, size);
    else if (cmd == MU_CMD_SEARCH_MAX_DRAM || cmd == MU_CMD_SEARCH_MAX_SRAM)
        return vst_mem_search_max(addr, unit, size);
    return 0;
}

UINT32  _mem_search_equ(const UINT64 const addr, UINT32 const unit, UINT32 const size, UINT32 const cmd, UINT32 const val)
{
    return vst_mem_search_equ(addr, unit, size, val);
}

void _mem_set_sram(UINT64 const addr, UINT32 const val, UINT32 bytes)
{
    vst_memset(addr, val, bytes);
}

void _mem_set_dram(UINT64 const addr, UINT32 const val, UINT32 bytes)
{
    if (omit) {
        omit = 0;
        vpage_t *pp_start = vram_vpage_map(addr);
        vpage_t *pp_end = vram_vpage_map(addr + bytes - 1);
        for (vpage_t *pp = pp_start; pp <= pp_end; pp++)
            untag_page(pp);
        return;
    }
    vst_memset(addr, val, bytes);
}

UINT8 _read_dram_8(UINT64 const addr)
{
    return (UINT8)vst_read_dram_8(addr);
}

UINT16 _read_dram_16(UINT64 const addr)
{
    return (UINT16)vst_read_dram_16(addr);
}

UINT32 _read_dram_32(UINT64 const addr)
{
    return (UINT32)vst_read_dram_32(addr);
}

void _write_dram_8(UINT64 const addr, UINT8 const val)
{
    vst_write_dram_8(addr, (UINT8)val);
}

void _write_dram_16(UINT64 const addr, UINT16 const val)
{
    vst_write_dram_16(addr, (UINT16)val);
}

void _write_dram_32(UINT64 const addr, UINT32 const val)
{
    vst_write_dram_32(addr, (UINT32)val);
}

void _set_bit_dram(UINT64 const base_addr, UINT32 const bit_offset)
{
    vst_set_bit_dram(base_addr, bit_offset);
}

void _clr_bit_dram(UINT64 const base_addr, UINT32 const bit_offset)
{
    vst_clr_bit_dram(base_addr, bit_offset);
}

BOOL32 _tst_bit_dram(UINT64 const base_addr, UINT32 const bit_offset)
{
    return vst_tst_bit_dram(base_addr, bit_offset);
}

/* dummy functions */
UINT32 disable_irq(void)
{
}

void enable_irq(void)
{
}

UINT32 disable_fiq(void)
{
}

void enable_fiq(void)
{
}

void flash_clear_irq(void)
{
}

void flash_finish(void)
{
}

void led(BOOL32 on)
{
}

void led_blink(void)
{
}

void ptimer_start(void)
{
}

UINT32 ptimer_stop(void)
{
    return 0;
}

#include <stdarg.h>
#include <stdio.h>
void uart_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void uart_print(char *s)
{
    uart_printf("%s\n", s);
}
