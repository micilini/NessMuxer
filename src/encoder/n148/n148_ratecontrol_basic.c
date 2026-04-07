#include "n148_ratecontrol_basic.h"

int n148_rc_basic_init(N148RateControlBasic* rc,
                       int width, int height, int fps, int bitrate_kbps)
{
    (void)width;
    (void)height;
    (void)fps;

    if (!rc)
        return -1;

    if (bitrate_kbps <= 1000) rc->qp = 26;
    else if (bitrate_kbps <= 2000) rc->qp = 22;
    else if (bitrate_kbps <= 4000) rc->qp = 18;
    else if (bitrate_kbps <= 8000) rc->qp = 16;
    else rc->qp = 14;

    return 0;
}

int n148_rc_basic_get_qp(const N148RateControlBasic* rc)
{
    return rc ? rc->qp : 22;
}