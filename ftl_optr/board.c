/**
 * board.c
 * Authors: Yun-Sheng Chang
 */

#include "ftl.h"
#include "board.h"
#include "cache.h"

void ftl_isr(void)
{
    #ifndef VST
    UINT32 bank;
    UINT32 flags;

    /* interrupt pending clear (ICU) */
    SETREG(APB_INT_STS, INTR_FLASH);

    for (bank = 0; bank < NUM_BANKS; bank++) {
        while (BSP_FSM(bank) != BANK_IDLE)
            ;
        flags = BSP_INTR(bank);
        if (flags == 0) {
            continue;
        }
        UINT32 fc = GETREG(BSP_CMD(bank));
        CLR_BSP_INTR(bank, flags);

        /* interrupt handling */
        if (flags & FIRQ_DATA_CORRUPT) {
            uart_printf("BSP interrupt at bank %u\n", bank);
            uart_printf("FIRQ_DATA_CORRUPT occurred at (%u, %u)\n",
                    GETREG(BSP_ROW_H(bank)) / PAGES_PER_BLK,
                    GETREG(BSP_ROW_H(bank)) % PAGES_PER_BLK
            );
        }
        if (flags & (FIRQ_BADBLK_H | FIRQ_BADBLK_L)) {
            uart_printf("BSP interrupt at bank: 0x%x\n", bank);
            if (fc == FC_COL_ROW_IN_PROG || fc == FC_IN_PROG || fc == FC_PROG) {
                uart_printf("Find runtime bad block on programming blk #%d\n",
                        GETREG(BSP_ROW_H(bank)) / PAGES_PER_BLK);
            } else {
                uart_printf("Find runtime bad block on erasing blk #%d\n",
                        GETREG(BSP_ROW_H(bank)) / PAGES_PER_BLK);
                ASSERT(fc == FC_ERASE);
            }
        }
    }
    #endif
}

/* This function writes a format mark to a page at bank #0, block #0 */
void write_format_mark(void)
{
    #ifndef VST
    #ifdef __GNUC__
    extern UINT32 size_of_firmware_image;
    UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) +
            BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
    #else
    extern UINT32 Image$$ER_CODE$$RO$$Length;
    extern UINT32 Image$$ER_RW$$RW$$Length;
    UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) +
            ((UINT32) &Image$$ER_RW$$RW$$Length);
    UINT32 firmware_image_pages =
            (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
    #endif

    UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;

    mem_set_dram(FTL_BUF_ADDR, 0, BYTES_PER_SECTOR);

    SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
    SETREG(FCP_BANK, REAL_BANK(0));
    SETREG(FCP_OPTION, FO_E | FO_B_W_DRDY);
    SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR);     // DRAM -> flash
    SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
    SETREG(FCP_COL, 0);
    SETREG(FCP_ROW_L(0), format_mark_page_offset);
    SETREG(FCP_ROW_H(0), format_mark_page_offset);

    /*
     * At this point, we do not have to check Waiting Room status
     * before issuing a command, because we have waited for all the
     * banks to become idle before returning from format().
     */
    SETREG(FCP_ISSUE, 0);

    /* wait for the FC_COL_ROW_IN_PROG command to be accepted by bank #0 */
    while ((GETREG(WR_STAT) & 0x00000001) != 0);

    /* wait until bank #0 finishes the write operation */
    while (BSP_FSM(0) != BANK_IDLE);
    #else
    mem_set_dram(FTL_BUF_ADDR, 15, BYTES_PER_SECTOR);
    nand_page_ptprogram(0, 0, 0, 0, 1, FTL_BUF_ADDR);
    #endif
}

/*
 * This function reads a flash page from (bank #0, block #0)
 * in order to check whether the SSD is formatted or not.
 */
BOOL32 check_format_mark(void)
{
    #ifndef VST
    #ifdef __GNUC__
    extern UINT32 size_of_firmware_image;
    UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) +
        BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
    #else
    extern UINT32 Image$$ER_CODE$$RO$$Length;
    extern UINT32 Image$$ER_RW$$RW$$Length;
    UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) +
            ((UINT32) &Image$$ER_RW$$RW$$Length);
    UINT32 firmware_image_pages =
            (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
    #endif

    UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;
    UINT32 temp;

    /* clear any flash interrupt flags that might have been set */
    flash_clear_irq();

    SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
    SETREG(FCP_BANK, REAL_BANK(0));
    SETREG(FCP_OPTION, FO_E);
    SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR);     // flash -> DRAM
    SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
    SETREG(FCP_COL, 0);
    SETREG(FCP_ROW_L(0), format_mark_page_offset);
    SETREG(FCP_ROW_H(0), format_mark_page_offset);

    /*
     * At this point, we do not have to check Waiting Room status
     * before issuing a command, because scan list loading has been
     * completed just before this function is called.
     */
    SETREG(FCP_ISSUE, 0);

    /* wait for the FC_COL_ROW_READ_OUT command to be accepted by bank #0 */
    while ((GETREG(WR_STAT) & 0x00000001) != 0);

    /* wait until bank #0 finishes the read operation */
    while (BSP_FSM(0) != BANK_IDLE);

    /* Now that the read operation is complete, we can check interrupt flags */
    temp = BSP_INTR(0) & FIRQ_ALL_FF;
    /* clear interrupt flags */
    CLR_BSP_INTR(0, 0xFF);

    if (temp != 0)
        /* the page contains all-0xFF (the format mark does not exist) */
        return FALSE;
    else
        /* the page contains sth other than 0xFF (must be the format mark) */
        return TRUE;
    #else
    UINT8 mark;
    nand_page_ptread(0, 0, 0, 0, 1, FTL_BUF_ADDR, RETURN_WHEN_DONE);
    mem_copy(&mark, FTL_BUF_ADDR, sizeof(UINT8));
    if (mark == 15)
        return TRUE;
    return FALSE;
    #endif
}

void set_intr_mask(UINT32 flags)
{
    #ifndef VST
    SETREG(INTR_MASK, flags);
    #endif
}

void set_fconf_pause(UINT32 flags)
{
    #ifndef VST
    SETREG(FCONF_PAUSE, flags);
    #endif
}

UINT32 num_busy_banks(void)
{
    #ifndef VST
    UINT32 n_busy = 0;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++) {
        if (_BSP_FSM(REAL_BANK(bank)) != BANK_IDLE) {
            n_busy++;
            stat_bank_busy(bank);
        }
    }
    return n_busy;
    #endif
}

extern UINT32 g_ftl_write_buf_id;
UINT32 is_write_buf_valid(void)
{
    #ifndef VST
    #if OPTION_FTL_TEST == 0
    UINT32 ret = 0;
    /* [start, end) are valid */
    UINT32 start = GETREG(BM_WRITE_LIMIT);
    UINT32 end = GETREG(SATA_WBUF_PTR);

    if (start < end) {
        ret = (g_ftl_write_buf_id >= start && g_ftl_write_buf_id < end);
    } else if (start > end) {
        ret = (g_ftl_write_buf_id >= start || g_ftl_write_buf_id < end);
    }

    return ret;
    #else
    return 1;
    #endif
    #endif
    return 1;
}

void wait_wr_free(void)
{
    while ((GETREG(WR_STAT) & 0x00000001) != 0)
        ;
}

void wait_bank_free(UINT32 const bank)
{
    while (_BSP_FSM(REAL_BANK(bank)) != BANK_IDLE)
        pool_write_buf();
}

void wait_rdbuf_free(UINT32 bufid)
{
    #ifndef VST
    #if OPTION_FTL_TEST == 0
    while (bufid == GETREG(SATA_RBUF_PTR));
    #endif
    #endif
}

void set_bm_read_limit(UINT32 bufid)
{
    #ifndef VST
    SETREG(BM_STACK_RDSET, bufid);
    SETREG(BM_STACK_RESET, 0x02);
    #endif
}
