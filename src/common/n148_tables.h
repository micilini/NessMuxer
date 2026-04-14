#ifndef N148_TABLES_H
#define N148_TABLES_H

#include <stdint.h>

enum {
    N148_TABLE_CLASS_INTRA_LUMA = 0,
    N148_TABLE_CLASS_INTRA_CHROMA = 1,
    N148_TABLE_CLASS_INTER_LUMA = 2,
    N148_TABLE_CLASS_INTER_CHROMA = 3,
    N148_TABLE_CLASS_COUNT = 4
};

int n148_table_block_class(int is_intra, int is_chroma);
int n148_table_deadzone_q24(int table_class, int scan_pos);
int n148_table_tail_drop_mag(int table_class, int scan_pos);

#endif
