/**
 * vpage.c
 * Authors: Yun-Sheng Chang
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "config.h"
#include "vpage.h"

/**
 * VST prohibits saving both host data and metadata on the same page.
 */
void tag_page(vpage_t *pp)
{
    if (pp->tagged)
        return;

    for (int i = 0; i < VST_SECTORS_PER_PAGE; i++)
        pp->sects[i].lba = -1;
    pp->tagged = 1;
}

void untag_page(vpage_t *pp)
{
    pp->tagged = 0;
}

void vpage_init(vpage_t *pp, uint8_t *data)
{
    pp->tagged = 0;
    pp->data = data;
}

void vpage_copy(vpage_t *dst, vpage_t *src, uint32_t sect, uint32_t n_sect)
{
    assert(sect + n_sect <= VST_SECTORS_PER_PAGE);

    if (src->tagged == 1) {
        /* host data */
        tag_page(dst);
        memcpy(&dst->sects[sect], &src->sects[sect], n_sect * sizeof(vsector_t));
    } else {
        /* metadata */
        untag_page(dst);
        if (dst->data == NULL)
            dst->data = (uint8_t *)malloc(VST_BYTES_PER_PAGE * sizeof(uint8_t));
        uint32_t start, length;
        start = sect * VST_BYTES_PER_SECTOR;
        length = n_sect * VST_BYTES_PER_SECTOR;
        if (src->data == NULL)
            /* only flash page may be NULL */
            memset(&dst->data[start], 0xff, length);
        else
            memcpy(&dst->data[start], &src->data[start], length);
    }
}

/* Should only be called by vflash */
void vpage_free(vpage_t *pp)
{
    pp->tagged = 0;
    free(pp->data);
    pp->data = NULL;
    /* dont need to reset lbas as it will be done when the page is tagged */
}

void vpage_serialize(vpage_t *pp, FILE *fp)
{
    int val_true = 1;
    int val_false = 0;

    fwrite(&pp->tagged, sizeof(int), 1, fp);
    switch (pp->tagged) {
    case 1:
        fwrite(pp->sects, sizeof(vsector_t), VST_SECTORS_PER_PAGE, fp);
        break;
    case 0:
        if (pp->data == NULL) {
            fwrite(&val_false, sizeof(int), 1, fp);
        } else {
            fwrite(&val_true, sizeof(int), 1, fp);
            fwrite(pp->data, sizeof(uint8_t), VST_BYTES_PER_PAGE, fp);
        }
        break;
    }
}

void vpage_deserialize(vpage_t *pp, FILE *fp)
{
    int exist_data;
    size_t ret;

    ret = fread(&pp->tagged, sizeof(int), 1, fp);
    if (ret == 0) {
        fprintf(stderr, "Fail to read file during deserializing flash.\n");
        exit(1);
    }
    switch (pp->tagged) {
    case 1:
        ret = fread(pp->sects, sizeof(vsector_t), VST_SECTORS_PER_PAGE, fp);
        if (ret == 0) {
            fprintf(stderr, "Fail to read file during deserializing flash.\n");
            exit(1);
        }
        break;
    case 0:
        ret = fread(&exist_data, sizeof(int), 1, fp);
        if (ret == 0) {
            fprintf(stderr, "Fail to read file during deserializing flash.\n");
            exit(1);
        }
        if (exist_data) {
            pp->data = malloc(VST_BYTES_PER_PAGE * sizeof(uint8_t));
            ret = fread(pp->data, sizeof(uint8_t), VST_BYTES_PER_PAGE, fp);
            if (ret == 0) {
                fprintf(stderr, "Fail to read file during deserializing flash.\n");
                exit(1);
            }
        } else {
            pp->data = NULL;
        }
        break;
    }
}
