/**
 * stat.h
 * Authors: Yun-Sheng Chang
 */

#ifndef STAT_H
#define STAT_H

void show_stat(void);
void stat_record_flush(UINT32 t);
void stat_record_para(UINT32 para);
void stat_bank_busy(UINT32 bank);
void stat_record_gc(UINT32 bank, UINT32 t);
void stat_record_insert(UINT32 bank);
void stat_record_full_write(UINT32 bank);
void stat_record_update_side(UINT32 bank);
void stat_record_update_center(UINT32 bank);
void stat_record_merge_write(UINT32 bank);
void stat_record_chkpt(void);
void stat_gc_vcount(UINT32 vcount);
void stat_gc_privcount(UINT32 privcount);
void stat_record_gc_degrade(UINT32 degrade);
void stat_gc_erase_sync(void);
void stat_gc_erase_async(void);
void stat_record_dep(UINT32 cnt_dep);
void stat_reclaim_log(void);
void stat_host_write(UINT32 sects);
void stat_host_read(UINT32 sects);
void stat_dirty_rate(UINT16 *n_dirty_bufs);
void stat_update_distance(UINT32 distance);
void stat_chkpt_page(void);
void stat_tag_page(void);
void stat_dep_page(void);
void stat_data_page(void);
void stat_chkpt_flush_page(UINT32 n_pg);
void stat_gc_flush_page(UINT32 n_pg);
void stat_manual_flush_page(UINT32 n_pg);
void stat_total_gc_blks(UINT32 n_blks);
void stat_constrained_gc_blks(UINT32 n_blks);
UINT32 stat_get_dist_median(void);
void stat_region_balance_factor(UINT32 bank, UINT32 region);
void stat_periodic_show_stat(void);

#endif // STAT_H
