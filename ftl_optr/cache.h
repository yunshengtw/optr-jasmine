/**
 * cache.h
 * Authors: Yun-Sheng Chang
 */

#ifndef CACHE_H
#define CACHE_H

void init_cache(void);
void pool_write_buf(void);
void enqueue(UINT32 const bank, UINT32 const lpn, UINT32 const buf_id,
             UINT32 const hole_left, UINT32 const hole_right, UINT32 const comp);
UINT32 dequeue(UINT32 const bank);
void wait_buf_complete(UINT32 const bank, UINT32 const buf_id);
void flush_write_buf(void);
UINT32 exist_in_cache(UINT32 const bank, UINT32 const lpn);
UINT32 is_cache_ent_dirty(UINT32 const bank, UINT32 const buf_id);
UINT32 get_cache_ent_epoch(UINT32 const bank, UINT32 const buf_id);
UINT16 get_cache_ent_pg_span(UINT32 const bank, UINT32 const buf_id);
UINT32 get_clean_cache_buf(UINT32 const bank);
void stall_cache(UINT32 const bank);
void release_cache(UINT32 const bank);
void cache_collect_dirty_rate(void);
UINT32 cache_get_total_dirty_bufs(void);

#endif // CACHE_H
