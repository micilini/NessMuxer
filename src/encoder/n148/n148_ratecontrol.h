#ifndef NESS_N148_RATECONTROL_H
#define NESS_N148_RATECONTROL_H

#include <stdint.h>

typedef enum {
    N148_RC_CQP = 0,  
    N148_RC_ABR = 1,   
    N148_RC_CBR = 2    
} N148RcMode;

typedef struct {
    N148RcMode mode;

   
    int    base_qp;

   
    int    target_bitrate_kbps;
    int    vbv_buffer_size_kbps; 
    int    vbv_max_rate_kbps;    

   
    int    width;
    int    height;
    int    fps;

   
    double bits_per_frame_target;
    double buffer_fullness;
    double buffer_size;
    double total_bits_spent;
    int    total_frames_coded;
    double avg_complexity;     
    double complexity_sum;
    double recent_bits_ema;
    double recent_complexity_ema;
    double last_frame_complexity;

   
    double pid_integral;
    double pid_prev_error;

   
    int    qp_min;
    int    qp_max;

   
    int    qp_offset_i;
    int    qp_offset_p;
    int    qp_offset_b;

   
    int    last_qp;
} N148RateControl;

int  n148_rc_init(N148RateControl* rc, N148RcMode mode,
                  int width, int height, int fps,
                  int target_bitrate_kbps, int base_qp);


int  n148_rc_get_frame_qp(N148RateControl* rc, int frame_type);


void n148_rc_update(N148RateControl* rc, int frame_type,
                    int bits_used, double frame_complexity);


void n148_rc_set_vbv(N148RateControl* rc,
                     int buffer_size_kbps, int max_rate_kbps);


void n148_rc_set_qp_range(N148RateControl* rc, int qp_min, int qp_max);


double n148_rc_get_avg_bitrate(const N148RateControl* rc);
double n148_rc_get_avg_qp(const N148RateControl* rc);
double n148_rc_get_buffer_fullness(const N148RateControl* rc);

#endif