/**
 * log.h
 * Authors: Yun-Sheng Chang
 */

#ifndef LOG_H
#define LOG_H

void init_log(void);
void insert_dep_entry(UINT32 const epoch_src, UINT16 const pg_span);
UINT32 is_depents_full(void);
void log_insert_mapent(UINT32 const lpn, UINT32 const ppn);
UINT32 reach_chkpt_threshold(void);
void record_mapent(void);
void record_tag(void);
void record_depent(void);
UINT32 reach_flush_depent(void);
void schedule_flush_depent(void);

#endif // LOG_H
