#include "n148_cabac_tables.h"

uint8_t n148_cabac_estimate_lps_range(uint8_t state, uint32_t current_range)
{
    uint32_t lps_range = 2u + ((uint32_t)(63 - (state & 63u)) >> 2);

    if (lps_range >= current_range)
        lps_range = current_range >> 1;
    if (lps_range == 0u)
        lps_range = 1u;

    return (uint8_t)lps_range;
}