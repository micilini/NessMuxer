#include "n148_inter.h"

static int mv_bit_cost(int ref_idx, int mvx_q4, int mvy_q4)
{
    int ax = (mvx_q4 < 0) ? -mvx_q4 : mvx_q4;
    int ay = (mvy_q4 < 0) ? -mvy_q4 : mvy_q4;
    return 4 + ref_idx * 2 + ax / 2 + ay / 2;
}

int n148_inter_choose_4x4(const uint8_t* cur_plane,
                          const uint8_t* const* ref_planes, int ref_count,
                          int stride,
                          int width, int height,
                          int bx, int by,
                          int sample_stride, int sample_offset,
                          int search_range,
                          int qp,
                          int intra_sad_hint,
                          N148InterDecision* out)
{
    N148QpelMotionResult mr;
    int lambda;

    if (!cur_plane || !ref_planes || ref_count <= 0 || !out)
        return -1;

    if (n148_motion_search_refs_qpel_4x4(cur_plane, stride,
                                         ref_planes, ref_count, stride,
                                         width, height,
                                         bx, by,
                                         sample_stride, sample_offset,
                                         search_range,
                                         &mr) != 0)
        return -1;

    lambda = 1 + (qp / 10);

    out->ref_idx = mr.ref_idx;
    out->mvx_q4 = mr.mvx_q4;
    out->mvy_q4 = mr.mvy_q4;
    out->cost_inter = mr.sad + lambda * mv_bit_cost(mr.ref_idx, mr.mvx_q4, mr.mvy_q4);
    out->cost_intra = intra_sad_hint + lambda * 3;

    if (mr.ref_idx == 0 && mr.mvx_q4 == 0 && mr.mvy_q4 == 0 && mr.sad <= 4)
        out->mode = 0;
    else if (out->cost_inter + 2 < out->cost_intra)
        out->mode = 1;
    else
        out->mode = 2;

    return 0;
}