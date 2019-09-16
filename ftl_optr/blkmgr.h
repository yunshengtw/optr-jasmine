/**
 * blkmgr.h
 * Authors: Yun-Sheng Chang
 */

#ifndef BLKMGR_H
#define BLKMGR_H

void init_blkmgr(void);
UINT32 blkmgr_get_map_blk(UINT32 const bank);
void blkmgr_toggle_map_blk_idx(void);
void blkmgr_erase_cur_map_blk(void);
void erase_all_blks(void);
UINT32 get_and_inc_active_blk(UINT32 const bank, UINT32 const region);
UINT32 get_log_blk(UINT32 const bank);
void revert_log_blk(UINT32 const bank);
UINT32 get_rsv_blk(UINT32 const bank, UINT32 const region);
UINT32 n_cur_rsv_blks(void);
void push_rsv(UINT32 const bank);
void inc_vcount(UINT32 const bank, UINT32 const blk);
void dec_vcount(UINT32 const bank, UINT32 const blk);
UINT32 blkmgr_reach_batch_gc_threshold(void);
UINT32 reach_gc_threshold(UINT32 const bank, UINT32 const region);
void garbage_collection(UINT32 const bank, UINT32 const region);
void blkmgr_erase_vt_blk(UINT32 const bank);
UINT32 blkmgr_reach_log_reclaim_threshold(void);
void blkmgr_reclaim_log(void);
UINT32 blkmgr_first_gc_triggered(void);

#endif // BLKMGR_H
