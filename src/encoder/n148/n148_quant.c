#include "n148_quant.h"

static const int g_quant_scale[6] = { 10, 11, 13, 14, 16, 18 };

static const int g_zigzag_4x4[16] = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15
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
    int16_t qcoeff_nat[16];
    int qstep = n148_quant_qstep_from_qp(qp);
    int i;
    int last_nonzero = -1;

    for (i = 0; i < 16; i++) {
        int c = coeff[i];
        int q;

        if (c >= 0)
            q = (c + qstep / 2) / qstep;
        else
            q = -(((-c) + qstep / 2) / qstep);

        qcoeff_nat[i] = (int16_t)q;
    }

    for (i = 0; i < 16; i++) {
        out_zigzag[i] = qcoeff_nat[g_zigzag_4x4[i]];
        if (out_zigzag[i] != 0)
            last_nonzero = i;
    }

    return last_nonzero + 1;
}

void n148_quant_unscan_zigzag_4x4(const int16_t* zigzag, int16_t* natural)
{
    int i;
    for (i = 0; i < 16; i++)
        natural[g_zigzag_4x4[i]] = zigzag[i];
}