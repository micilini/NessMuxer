#include "n148_quant.h"
#include "../../common/n148_tables.h"

static const int g_quant_scale[6] = { 10, 11, 13, 14, 16, 18 };

static const int g_zigzag_4x4[16] = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15
};

static const uint8_t g_inv_zigzag_4x4[16] = {
    0, 1, 5, 6,
    2, 4, 7, 12,
    3, 8, 11, 13,
    9, 10, 14, 15
};

static int clamp_qp(int qp)
{
    if (qp < 0) return 0;
    if (qp > 51) return 51;
    return qp;
}

int n148_quant_qstep_from_qp(int qp)
{
    int rem;
    int div;

    qp = clamp_qp(qp);
    rem = qp % 6;
    div = qp / 6;
    return g_quant_scale[rem] << div;
}

int n148_quantize_4x4(const int16_t* coeff, int16_t* out_zigzag, int qp)
{
    return n148_quantize_4x4_tuned(coeff, out_zigzag, qp, 1, 0);
}

int n148_quantize_4x4_tuned(const int16_t* coeff, int16_t* out_zigzag, int qp, int is_intra, int is_chroma)
{
    int16_t qcoeff_nat[16];
    int qstep = n148_quant_qstep_from_qp(qp);
    int table_class = n148_table_block_class(is_intra, is_chroma);
    int i;
    int last_nonzero = -1;

    for (i = 0; i < 16; i++) {
        int c = coeff[i];
        int mag = (c < 0) ? -c : c;
        int scan_pos = g_inv_zigzag_4x4[i];
        int deadzone_q24 = n148_table_deadzone_q24(table_class, scan_pos);
        int deadzone = (qstep * deadzone_q24 + 12) / 24;
        int q;

        if (mag <= deadzone) {
            q = 0;
        } else {
            q = (mag - deadzone + qstep / 2) / qstep;
            if (c < 0)
                q = -q;
        }

        qcoeff_nat[i] = (int16_t)q;
    }

    for (i = 0; i < 16; i++) {
        out_zigzag[i] = qcoeff_nat[g_zigzag_4x4[i]];
        if (out_zigzag[i] != 0)
            last_nonzero = i;
    }

    while (last_nonzero >= 0) {
        int v = out_zigzag[last_nonzero];
        int mag = (v < 0) ? -v : v;
        int drop_mag = n148_table_tail_drop_mag(table_class, last_nonzero);

        if (drop_mag <= 0 || mag > drop_mag)
            break;

        out_zigzag[last_nonzero] = 0;
        while (last_nonzero >= 0 && out_zigzag[last_nonzero] == 0)
            last_nonzero--;
    }

    return last_nonzero + 1;
}

void n148_quant_unscan_zigzag_4x4(const int16_t* zigzag, int16_t* natural)
{
    int i;
    for (i = 0; i < 16; i++)
        natural[g_zigzag_4x4[i]] = zigzag[i];
}