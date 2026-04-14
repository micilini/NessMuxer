#include "n148_tables.h"

static const uint8_t g_deadzone_q24[N148_TABLE_CLASS_COUNT][16] = {
    /* intra luma */   { 12, 15, 15, 15, 18, 18, 18, 18, 21, 21, 21, 21, 21, 21, 21, 21 },
    /* intra chroma */ { 12, 21, 21, 21, 24, 24, 24, 24, 27, 27, 27, 27, 27, 27, 27, 27 },
    /* inter luma */   {  8, 18, 18, 18, 21, 21, 21, 24, 30, 30, 30, 30, 30, 30, 30, 30 },
    /* inter chroma */ {  8, 24, 24, 24, 27, 27, 30, 30, 33, 33, 33, 33, 33, 33, 33, 33 }
};

static const uint8_t g_tail_drop_mag[N148_TABLE_CLASS_COUNT][16] = {
    /* intra luma */   { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1 },
    /* intra chroma */ { 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
    /* inter luma */   { 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
    /* inter chroma */ { 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 }
};

int n148_table_block_class(int is_intra, int is_chroma)
{
    if (is_intra)
        return is_chroma ? N148_TABLE_CLASS_INTRA_CHROMA : N148_TABLE_CLASS_INTRA_LUMA;
    return is_chroma ? N148_TABLE_CLASS_INTER_CHROMA : N148_TABLE_CLASS_INTER_LUMA;
}

int n148_table_deadzone_q24(int table_class, int scan_pos)
{
    if (table_class < 0 || table_class >= N148_TABLE_CLASS_COUNT)
        table_class = N148_TABLE_CLASS_INTRA_LUMA;
    if (scan_pos < 0)
        scan_pos = 0;
    if (scan_pos > 15)
        scan_pos = 15;
    return (int)g_deadzone_q24[table_class][scan_pos];
}

int n148_table_tail_drop_mag(int table_class, int scan_pos)
{
    if (table_class < 0 || table_class >= N148_TABLE_CLASS_COUNT)
        table_class = N148_TABLE_CLASS_INTRA_LUMA;
    if (scan_pos < 0)
        scan_pos = 0;
    if (scan_pos > 15)
        scan_pos = 15;
    return (int)g_tail_drop_mag[table_class][scan_pos];
}
