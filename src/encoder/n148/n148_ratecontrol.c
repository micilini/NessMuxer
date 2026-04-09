#include "n148_ratecontrol.h"
#include <string.h>
#include <math.h>


#define PID_KP  0.6
#define PID_KI  0.05
#define PID_KD  0.1


#define COMPLEXITY_ALPHA 0.2

static int clamp_qp(const N148RateControl* rc, int qp)
{
    if (qp < rc->qp_min) return rc->qp_min;
    if (qp > rc->qp_max) return rc->qp_max;
    return qp;
}

int n148_rc_init(N148RateControl* rc, N148RcMode mode,
                 int width, int height, int fps,
                 int target_bitrate_kbps, int base_qp)
{
    if (!rc) return -1;

    memset(rc, 0, sizeof(*rc));
    rc->mode = mode;
    rc->width = width;
    rc->height = height;
    rc->fps = fps > 0 ? fps : 30;
    rc->base_qp = base_qp > 0 ? base_qp : 22;
    rc->target_bitrate_kbps = target_bitrate_kbps > 0 ? target_bitrate_kbps : 2000;

    rc->qp_min = 10;
    rc->qp_max = 51;
    rc->qp_offset_i = 0;
    rc->qp_offset_p = 2;
    rc->qp_offset_b = 4;

    rc->last_qp = rc->base_qp;

   
    rc->bits_per_frame_target =
        (double)rc->target_bitrate_kbps * 1000.0 / (double)rc->fps;

   
    rc->buffer_size = rc->target_bitrate_kbps * 1000.0;
    rc->buffer_fullness = rc->buffer_size * 0.5;

    rc->avg_complexity = 1.0;

    return 0;
}

int n148_rc_get_frame_qp(N148RateControl* rc, int frame_type)
{
    int qp;
    int offset = 0;

    if (!rc) return 22;

    if (rc->mode == N148_RC_CQP) {
       
        switch (frame_type) {
            case 1: offset = rc->qp_offset_i; break;
            case 2: offset = rc->qp_offset_p; break;
            case 3: offset = rc->qp_offset_b; break;
            default: offset = 0; break;
        }
        qp = clamp_qp(rc, rc->base_qp + offset);
        rc->last_qp = qp;
        return qp;
    }

   
    {
        double error;        
        double derivative;
        double pid_output;

        error = rc->bits_per_frame_target - 
                (rc->total_frames_coded > 0
                    ? rc->total_bits_spent / (double)rc->total_frames_coded
                    : rc->bits_per_frame_target);

        rc->pid_integral += error;

       
        if (rc->pid_integral > rc->bits_per_frame_target * 10.0)
            rc->pid_integral = rc->bits_per_frame_target * 10.0;
        if (rc->pid_integral < -rc->bits_per_frame_target * 10.0)
            rc->pid_integral = -rc->bits_per_frame_target * 10.0;

        derivative = error - rc->pid_prev_error;
        rc->pid_prev_error = error;

        pid_output = PID_KP * error
                   + PID_KI * rc->pid_integral
                   + PID_KD * derivative;

       
        {
            double qp_adj = -pid_output / (rc->bits_per_frame_target * 0.1 + 1.0);
            qp = (int)(rc->base_qp + qp_adj + 0.5);
        }

       
        switch (frame_type) {
            case 1: qp += rc->qp_offset_i; break;
            case 2: qp += rc->qp_offset_p; break;
            case 3: qp += rc->qp_offset_b; break;
        }

        qp = clamp_qp(rc, qp);
        rc->last_qp = qp;
        return qp;
    }
}

void n148_rc_update(N148RateControl* rc, int frame_type,
                    int bits_used, double frame_complexity)
{
    if (!rc) return;

    (void)frame_type;

    rc->total_bits_spent += (double)bits_used;
    rc->total_frames_coded++;

   
    if (frame_complexity > 0.0) {
        rc->complexity_sum += frame_complexity;
        rc->avg_complexity = rc->avg_complexity * (1.0 - COMPLEXITY_ALPHA)
                           + frame_complexity * COMPLEXITY_ALPHA;
    }

   
    if (rc->buffer_size > 0.0) {
        rc->buffer_fullness += rc->bits_per_frame_target - (double)bits_used;

       
        if (rc->buffer_fullness < 0.0)
            rc->buffer_fullness = 0.0;
        if (rc->buffer_fullness > rc->buffer_size)
            rc->buffer_fullness = rc->buffer_size;

       
        if (rc->buffer_fullness < rc->buffer_size * 0.1) {
           
            if (rc->base_qp < rc->qp_max - 2)
                rc->base_qp += 2;
        } else if (rc->buffer_fullness > rc->buffer_size * 0.9) {
           
            if (rc->base_qp > rc->qp_min + 2)
                rc->base_qp -= 1;
        }
    }
}

void n148_rc_set_vbv(N148RateControl* rc,
                     int buffer_size_kbps, int max_rate_kbps)
{
    if (!rc) return;

    rc->vbv_buffer_size_kbps = buffer_size_kbps;
    rc->vbv_max_rate_kbps = max_rate_kbps;

    if (buffer_size_kbps > 0)
        rc->buffer_size = buffer_size_kbps * 1000.0;
    if (max_rate_kbps > 0) {
        double target = rc->bits_per_frame_target;
        double cap = (double)max_rate_kbps * 1000.0 / (double)rc->fps;
        if (target > cap)
            rc->bits_per_frame_target = cap;
    }

    rc->buffer_fullness = rc->buffer_size * 0.5;
}

void n148_rc_set_qp_range(N148RateControl* rc, int qp_min, int qp_max)
{
    if (!rc) return;
    rc->qp_min = qp_min < 0 ? 0 : (qp_min > 51 ? 51 : qp_min);
    rc->qp_max = qp_max < 0 ? 0 : (qp_max > 51 ? 51 : qp_max);
    if (rc->qp_min > rc->qp_max) rc->qp_min = rc->qp_max;
}

double n148_rc_get_avg_bitrate(const N148RateControl* rc)
{
    if (!rc || rc->total_frames_coded == 0) return 0.0;
    return (rc->total_bits_spent / (double)rc->total_frames_coded)
         * (double)rc->fps / 1000.0;
}

double n148_rc_get_avg_qp(const N148RateControl* rc)
{
   
    return rc ? (double)rc->last_qp : 22.0;
}

double n148_rc_get_buffer_fullness(const N148RateControl* rc)
{
    if (!rc || rc->buffer_size <= 0.0) return 0.0;
    return rc->buffer_fullness / rc->buffer_size;
}