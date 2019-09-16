/**
 * pgmap.h
 * Authors: Yun-Sheng Chang
 */

#ifndef PGMAP_H
#define PGMAP_H

void init_pgmap(void);
void pgmap_close(void);
void pgmap_dump_l2p(void);
void set_ppn(UINT32 const lpn, UINT32 const ppn);
UINT32 get_ppn(UINT32 const lpn);
void set_lpn(UINT32 const bank, UINT32 const region, UINT32 const page, UINT32 const lpn);
UINT32 get_lpn(UINT32 const bank, UINT32 const region, UINT32 const page);
UINT32 get_active_ppn(UINT32 const bank, UINT32 const region);
UINT32 get_and_inc_active_ppn(UINT32 const bank, UINT32 const region);
void pgmap_trim(UINT32 const lpn, UINT32 const n_pages);
UINT32 get_log_ppn(UINT32 const bank);
void revert_log_ppn(UINT32 const bank);
void pgmap_restore_map_table(void);
void pgmap_persist_map_table(void);

#endif // PGMAP_H
