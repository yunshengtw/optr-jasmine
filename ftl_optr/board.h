/**
 * board.h
 * Authors: Yun-Sheng Chang
 */

#ifndef BOARD_H
#define BOARD_H

void ftl_isr(void);
void write_format_mark(void);
BOOL32 check_format_mark(void);
void set_intr_mask(UINT32 flags);
void set_fconf_pause(UINT32 flags);
UINT32 num_busy_banks(void);
UINT32 is_write_buf_valid(void);
void wait_wr_free(void);
void wait_bank_free(UINT32 const bank);
void wait_rdbuf_free(UINT32 bufid);
void set_bm_read_limit(UINT32 bufid);

#endif // BOARD_H
