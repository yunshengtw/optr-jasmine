/**
 * recovery.h
 * Authors: Yun-Sheng Chang
 */

#ifndef RECOVERY_H
#define RECOVERY_H

void init_recovery(void);
int analyze(void);
void rebuild(void);
UINT32 recovery_get_epoch_incomplete(void);

#endif // RECOVERY_H
