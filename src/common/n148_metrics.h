#ifndef NESS_N148_METRICS_H
#define NESS_N148_METRICS_H

#include <stdint.h>


double n148_psnr(const uint8_t* orig, const uint8_t* recon,
                 int width, int height, int stride_orig, int stride_recon);


double n148_psnr_y_nv12(const uint8_t* orig, const uint8_t* recon,
                        int width, int height);


double n148_ssim(const uint8_t* orig, const uint8_t* recon,
                 int width, int height, int stride_orig, int stride_recon);


double n148_ssim_y_nv12(const uint8_t* orig, const uint8_t* recon,
                        int width, int height);

#endif