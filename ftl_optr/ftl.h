// Copyright 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//
// GreedyFTL header file
//
// Author; Sang-Phil Lim (SKKU VLDB Lab.)
//

#ifndef FTL_H
#define FTL_H

#include "jasmine.h"

/////////////////
// DRAM buffers
/////////////////

#define NUM_RW_BUFFERS      ((DRAM_SIZE - DRAM_BYTES_OTHER) / BYTES_PER_PAGE - 1)
#define NUM_RD_BUFFERS      (((NUM_RW_BUFFERS / 8) + NUM_BANKS - 1) / NUM_BANKS * NUM_BANKS)
#define NUM_WR_BUFFERS      (NUM_RW_BUFFERS - NUM_RD_BUFFERS)
#define NUM_COPY_BUFFERS    NUM_BANKS_MAX
#define NUM_FTL_BUFFERS     NUM_BANKS
#define NUM_HIL_BUFFERS     1
#define NUM_TEMP_BUFFERS    1
#define NUM_CACHE_BUFFERS   512
#define NUM_CACHE_BUFFERS_PER_BANK  (NUM_CACHE_BUFFERS / NUM_BANKS)
#define NUM_HEAD_BUFFERS    NUM_BANKS
#define NUM_DEP_BUFFERS     1
#define NUM_CHKPT_BUFFERS   (2 * NUM_BANKS)

#define DRAM_BYTES_OTHER    ((NUM_COPY_BUFFERS + NUM_FTL_BUFFERS + NUM_HIL_BUFFERS + NUM_TEMP_BUFFERS + NUM_CACHE_BUFFERS + NUM_HEAD_BUFFERS + NUM_DEP_BUFFERS + NUM_CHKPT_BUFFERS) * BYTES_PER_PAGE + BAD_BLK_BMP_BYTES + PAGE_MAP_BYTES + LPNS_BYTES + VCOUNT_BYTES + EPOCHS_BYTES + BLK_LIST_BYTES + BLK_TIME_BYTES)

#define WR_BUF_PTR(BUF_ID)  (WR_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define WR_BUF_ID(BUF_PTR)  ((((UINT32)BUF_PTR) - WR_BUF_ADDR) / BYTES_PER_PAGE)
#define RD_BUF_PTR(BUF_ID)  (RD_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define RD_BUF_ID(BUF_PTR)  ((((UINT32)BUF_PTR) - RD_BUF_ADDR) / BYTES_PER_PAGE)

#define _COPY_BUF(RBANK)    (COPY_BUF_ADDR + (RBANK) * BYTES_PER_PAGE)
#define COPY_BUF(BANK)      _COPY_BUF(REAL_BANK(BANK))
#define FTL_BUF(BANK)       (FTL_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))
#define CACHE_BUF(BANK, BUF_ID)     (CACHE_BUF_ADDR + ((BANK) * NUM_CACHE_BUFFERS_PER_BANK + (BUF_ID)) * BYTES_PER_PAGE)
#define HEAD_BUF(BANK)      (HEAD_BUF_ADDR + (BANK) * BYTES_PER_PAGE)
#define CHKPT_BUF(BUF_ID)   (CHKPT_BUF_ADDR + (BUF_ID) * BYTES_PER_PAGE)
#define EPOCHS(BANK, BUF_ID)    (EPOCHS_ADDR + ((BANK) * NUM_CACHE_BUFFERS_PER_BANK + (BUF_ID)) * sizeof(UINT32))
#define LPNS(BANK, REGION, PAGE)    (LPNS_ADDR + (BANK) * LPNS_BYTES_PER_BANK + (REGION) * LPNS_BYTES_PER_REG + (PAGE) * sizeof(UINT32))
#define BLK_TIME(BANK, BLK) (BLK_TIME_ADDR + (BANK * VBLKS_PER_BANK + BLK) * sizeof(UINT32))
#define RECOVERY_PAGE_EPOCH(LPN)    (RECOVERY_PAGE_EPOCH_ADDR + (LPN) * sizeof(UINT32))

///////////////////////////////
// DRAM segmentation
///////////////////////////////

#define RD_BUF_ADDR         DRAM_BASE                                       // base address of SATA read buffers
#define RD_BUF_BYTES        (NUM_RD_BUFFERS * BYTES_PER_PAGE)

#define WR_BUF_ADDR         (RD_BUF_ADDR + RD_BUF_BYTES)                    // base address of SATA write buffers
#define WR_BUF_BYTES        (NUM_WR_BUFFERS * BYTES_PER_PAGE)

#define COPY_BUF_ADDR       (WR_BUF_ADDR + WR_BUF_BYTES)                    // base address of flash copy buffers
#define COPY_BUF_BYTES      (NUM_COPY_BUFFERS * BYTES_PER_PAGE)

#define FTL_BUF_ADDR        (COPY_BUF_ADDR + COPY_BUF_BYTES)                // a buffer dedicated to FTL internal purpose
#define FTL_BUF_BYTES       (NUM_FTL_BUFFERS * BYTES_PER_PAGE)

#define HIL_BUF_ADDR        (FTL_BUF_ADDR + FTL_BUF_BYTES)                  // a buffer dedicated to HIL internal purpose
#define HIL_BUF_BYTES       (NUM_HIL_BUFFERS * BYTES_PER_PAGE)

#define TEMP_BUF_ADDR       (HIL_BUF_ADDR + HIL_BUF_BYTES)                  // general purpose buffer
#define TEMP_BUF_BYTES      (NUM_TEMP_BUFFERS * BYTES_PER_PAGE)

#define CACHE_BUF_ADDR      (TEMP_BUF_ADDR + TEMP_BUF_BYTES)
/* We use cache buffer to store temp page entries during recovery */
#define RECOVERY_ADDR       (CACHE_BUF_ADDR)
#define CACHE_BUF_BYTES     (NUM_CACHE_BUFFERS * BYTES_PER_PAGE)

#define HEAD_BUF_ADDR       (CACHE_BUF_ADDR + CACHE_BUF_BYTES)
#define HEAD_BUF_BYTES      (NUM_HEAD_BUFFERS * BYTES_PER_PAGE)

#define DEP_BUF_ADDR        (HEAD_BUF_ADDR + HEAD_BUF_BYTES)
#define DEP_BUF_BYTES       (NUM_DEP_BUFFERS * BYTES_PER_PAGE)

#define CHKPT_BUF_ADDR      (DEP_BUF_ADDR + DEP_BUF_BYTES)
#define CHKPT_BUF_BYTES     (NUM_CHKPT_BUFFERS * BYTES_PER_PAGE)

#define PAGE_MAP_ADDR       (CHKPT_BUF_ADDR + CHKPT_BUF_BYTES)          // page mapping table
#define PAGE_MAP_BYTES      ((NUM_LPAGES * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define LPNS_ADDR           (PAGE_MAP_ADDR + PAGE_MAP_BYTES)
#define LPNS_BYTES_PER_REG  ((PAGES_PER_VBLK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)
#define LPNS_BYTES_PER_BANK (LPNS_BYTES_PER_REG * NUM_REGIONS)
#define LPNS_BYTES          (NUM_BANKS * LPNS_BYTES_PER_BANK)

#define BAD_BLK_BMP_ADDR    (LPNS_ADDR + LPNS_BYTES)              // bitmap of initial bad blocks
#define BAD_BLK_BMP_BYTES   (((NUM_VBLKS / 8) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define VCOUNT_ADDR         (BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES)
#define VCOUNT_BYTES        ((NUM_BANKS * VBLKS_PER_BANK * sizeof(UINT16) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define EPOCHS_ADDR         (VCOUNT_ADDR + VCOUNT_BYTES)
#define EPOCHS_BYTES        ((NUM_CACHE_BUFFERS * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define BLK_LIST_ADDR       (EPOCHS_ADDR + EPOCHS_BYTES)
#define BLK_LIST_BYTES      ((NUM_BANKS * VBLKS_PER_BANK * sizeof(UINT16) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define BLK_TIME_ADDR       (BLK_LIST_ADDR + BLK_LIST_BYTES)
#define BLK_TIME_BYTES      ((NUM_BANKS * VBLKS_PER_BANK * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

// #define BLKS_PER_BANK        VBLKS_PER_BANK

/**
 * RECOVERY_PAGE_EPOCH overlaps with read write buffers as it's only used
 * during recovery.
 */
#define RECOVERY_PAGE_EPOCH_ADDR    DRAM_BASE
#define RECOVERY_PAGE_EPOCH_BYTES   ((NUM_LPAGES * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define ROUND_UP(x, a) (((x) + (a) - 1) / (a) * (a))
#define ROUND_DOWN(x, a) ((x) / (a) * (a))
#define VC_MAX 0xCDCD
#define NUM_LOG_BLKS_PER_BANK 2
//#define NUM_MAPENTS_PER_PAGE 3600
#define NUM_MAPENTS_PER_PAGE 1800
//#define NUM_DEPENTS_PER_PAGE 1500
#define NUM_DEPENTS_PER_PAGE 750
#define NUM_BLKS_RSV 1600
#define NUM_REGIONS 2
#define OPTION_SHOW_ERASE_BLK_INFO 0
//#define GC_THRESHOLD 50
#define GC_THRESHOLD 120
#define BATCH_GC_THRESHOLD 16
#define AUTO_FLUSH 5

///////////////////////////////
// FTL public functions
///////////////////////////////

void ftl_open(void);
void ftl_close(void);
void ftl_read(UINT32 const lba, UINT32 const num_sectors);
void ftl_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_test_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_flush(void);
UINT32 ftl_prefix_flush(void);
void ftl_standby(void);
void ftl_idle(void);
void ftl_isr(void);
void ftl_trim(UINT32 const reserved, UINT32 const n_range_ents);
UINT32 ftl_get_epoch_incomplete(void);
void ftl_recovery(void);
UINT32 ftl_get_epoch(void);

#endif //FTL_H
