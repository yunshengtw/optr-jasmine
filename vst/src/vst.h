/**
 * vst.h
 * Authors: Yun-Sheng Chang
 */

#ifndef VST_H
#define VST_H

#define MAX_SIZE_TRACE 180000000
/* trace struct */
struct trace_ent {
    uint64_t lba;
    uint32_t sec_num, rw;
};

void simulate_crash(void);

#endif // VST_H
