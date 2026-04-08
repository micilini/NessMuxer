#include <stdio.h>
#include <stdint.h>

#include "../src/common/interpolation.h"

int main(void)
{
    uint8_t plane[16] = {
         0,  64, 128, 192,
        16,  80, 144, 208,
        32,  96, 160, 224,
        48, 112, 176, 240
    };

    uint8_t a = n148_interp_sample_qpel(plane, 4, 4, 4, 4, 4, 1, 0);
    uint8_t b = n148_interp_sample_qpel(plane, 4, 4, 4, 6, 4, 1, 0);
    uint8_t c = n148_interp_sample_qpel(plane, 4, 4, 4, 4, 6, 1, 0);

    if (a != 80)
        return 1;

    if (b < 96 || b > 112)
        return 2;

    if (c < 84 || c > 100)
        return 3;

    printf("=== N.148 Interpolation Test (Fase 5.2) ===\n");
    printf("  [PASS] integer sample OK\n");
    printf("  [PASS] qpel horizontal interpolation OK\n");
    printf("  [PASS] qpel vertical interpolation OK\n");
    return 0;
}