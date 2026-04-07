#ifndef NESS_N148_RATECONTROL_BASIC_H
#define NESS_N148_RATECONTROL_BASIC_H

typedef struct {
    int qp;
} N148RateControlBasic;

int n148_rc_basic_init(N148RateControlBasic* rc,
                       int width, int height, int fps, int bitrate_kbps);

int n148_rc_basic_get_qp(const N148RateControlBasic* rc);

#endif