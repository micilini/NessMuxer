#include "n148_metrics.h"
#include <math.h>

double n148_psnr(const uint8_t* orig, const uint8_t* recon,
                 int width, int height, int stride_orig, int stride_recon)
{
    double mse = 0.0;
    int x, y;
    int count = 0;

    if (!orig || !recon || width <= 0 || height <= 0)
        return 0.0;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            double diff = (double)orig[y * stride_orig + x]
                        - (double)recon[y * stride_recon + x];
            mse += diff * diff;
            count++;
        }
    }

    if (count == 0) return 0.0;
    mse /= (double)count;

    if (mse < 1e-10) return 99.99;

    return 10.0 * log10(255.0 * 255.0 / mse);
}

double n148_psnr_y_nv12(const uint8_t* orig, const uint8_t* recon,
                        int width, int height)
{
    return n148_psnr(orig, recon, width, height, width, width);
}


#define SSIM_C1 (6.5025)   
#define SSIM_C2 (58.5225)  
#define SSIM_WINDOW 8

double n148_ssim(const uint8_t* orig, const uint8_t* recon,
                 int width, int height, int stride_orig, int stride_recon)
{
    double ssim_sum = 0.0;
    int blocks = 0;
    int bx, by, x, y;

    if (!orig || !recon || width < SSIM_WINDOW || height < SSIM_WINDOW)
        return 0.0;

    for (by = 0; by <= height - SSIM_WINDOW; by += SSIM_WINDOW) {
        for (bx = 0; bx <= width - SSIM_WINDOW; bx += SSIM_WINDOW) {
            double sum_o = 0, sum_r = 0;
            double sum_oo = 0, sum_rr = 0, sum_or = 0;
            double mu_o, mu_r, sig_oo, sig_rr, sig_or;
            double num, den;
            int n = SSIM_WINDOW * SSIM_WINDOW;

            for (y = 0; y < SSIM_WINDOW; y++) {
                for (x = 0; x < SSIM_WINDOW; x++) {
                    double o = orig[(by + y) * stride_orig + bx + x];
                    double r = recon[(by + y) * stride_recon + bx + x];
                    sum_o  += o;
                    sum_r  += r;
                    sum_oo += o * o;
                    sum_rr += r * r;
                    sum_or += o * r;
                }
            }

            mu_o = sum_o / n;
            mu_r = sum_r / n;
            sig_oo = sum_oo / n - mu_o * mu_o;
            sig_rr = sum_rr / n - mu_r * mu_r;
            sig_or = sum_or / n - mu_o * mu_r;

            num = (2.0 * mu_o * mu_r + SSIM_C1) * (2.0 * sig_or + SSIM_C2);
            den = (mu_o * mu_o + mu_r * mu_r + SSIM_C1)
                * (sig_oo + sig_rr + SSIM_C2);

            ssim_sum += (den > 0.0) ? (num / den) : 1.0;
            blocks++;
        }
    }

    return blocks > 0 ? ssim_sum / (double)blocks : 0.0;
}

double n148_ssim_y_nv12(const uint8_t* orig, const uint8_t* recon,
                        int width, int height)
{
    return n148_ssim(orig, recon, width, height, width, width);
}