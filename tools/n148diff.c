
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "common/n148_metrics.h"

static void write_diff_frame(FILE* diff_f,
                             const uint8_t* orig, const uint8_t* recon,
                             int width, int height)
{
    int frame_y_size = width * height;
    int frame_uv_size = width * height / 2;
    uint8_t* diff_y;
    uint8_t* diff_uv;
    int i;

    diff_y = (uint8_t*)malloc((size_t)(frame_y_size + frame_uv_size));
    if (!diff_y) return;
    diff_uv = diff_y + frame_y_size;

   
    for (i = 0; i < frame_y_size; i++) {
        int d = (int)orig[i] - (int)recon[i];
        if (d < 0) d = -d;
        d *= 4;
        if (d > 255) d = 255;
        diff_y[i] = (uint8_t)d;
    }

   
    memset(diff_uv, 128, (size_t)frame_uv_size);

    fwrite(diff_y, 1, (size_t)(frame_y_size + frame_uv_size), diff_f);
    free(diff_y);
}

int main(int argc, char* argv[])
{
    const char* orig_path = NULL;
    const char* recon_path = NULL;
    const char* diff_path = NULL;
    int width = 0, height = 0;
    int i;

    FILE* f_orig = NULL;
    FILE* f_recon = NULL;
    FILE* f_diff = NULL;

    int frame_size;
    uint8_t* buf_orig = NULL;
    uint8_t* buf_recon = NULL;

    int frame_count = 0;
    double psnr_sum = 0.0;
    double ssim_sum = 0.0;
    double worst_psnr = 999.0;
    int worst_frame = 0;
    double best_psnr = 0.0;
    int best_frame = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--diff") == 0 && i + 1 < argc) {
            diff_path = argv[++i];
        } else if (!orig_path) {
            orig_path = argv[i];
        } else if (!recon_path) {
            recon_path = argv[i];
        }
    }

    if (!orig_path || !recon_path || width <= 0 || height <= 0) {
        fprintf(stderr,
            "Usage: n148diff <original.nv12> <decoded.nv12> "
            "--width W --height H [--diff diff.nv12]\n");
        return 1;
    }

    frame_size = width * height * 3 / 2;

    f_orig = fopen(orig_path, "rb");
    if (!f_orig) { fprintf(stderr, "Cannot open: %s\n", orig_path); return 1; }

    f_recon = fopen(recon_path, "rb");
    if (!f_recon) { fprintf(stderr, "Cannot open: %s\n", recon_path); fclose(f_orig); return 1; }

    if (diff_path) {
        f_diff = fopen(diff_path, "wb");
        if (!f_diff) fprintf(stderr, "WARN: Cannot create diff file: %s\n", diff_path);
    }

    buf_orig = (uint8_t*)malloc((size_t)frame_size);
    buf_recon = (uint8_t*)malloc((size_t)frame_size);
    if (!buf_orig || !buf_recon) {
        fprintf(stderr, "Out of memory\n");
        goto done;
    }

    printf("=== n148diff ===\n");
    printf("  Original: %s\n", orig_path);
    printf("  Decoded:  %s\n", recon_path);
    printf("  Size:     %dx%d  frame=%d bytes\n\n", width, height, frame_size);

    while (1) {
        size_t r1 = fread(buf_orig, 1, (size_t)frame_size, f_orig);
        size_t r2 = fread(buf_recon, 1, (size_t)frame_size, f_recon);

        if (r1 < (size_t)frame_size || r2 < (size_t)frame_size)
            break;

        {
            double psnr_y = n148_psnr_y_nv12(buf_orig, buf_recon, width, height);
            double ssim_y = n148_ssim_y_nv12(buf_orig, buf_recon, width, height);

            psnr_sum += psnr_y;
            ssim_sum += ssim_y;

            if (psnr_y < worst_psnr) {
                worst_psnr = psnr_y;
                worst_frame = frame_count;
            }
            if (psnr_y > best_psnr) {
                best_psnr = psnr_y;
                best_frame = frame_count;
            }

            printf("  Frame %4d: PSNR=%.2f dB  SSIM=%.4f\n",
                   frame_count, psnr_y, ssim_y);

            if (f_diff)
                write_diff_frame(f_diff, buf_orig, buf_recon, width, height);
        }

        frame_count++;
    }

    if (frame_count == 0) {
        printf("  No complete frames found.\n");
        goto done;
    }

    printf("\n  ========== Summary ==========\n");
    printf("  Frames:     %d\n", frame_count);
    printf("  Avg PSNR:   %.2f dB\n", psnr_sum / frame_count);
    printf("  Avg SSIM:   %.4f\n", ssim_sum / frame_count);
    printf("  Worst:      frame %d (PSNR=%.2f dB)\n", worst_frame, worst_psnr);
    printf("  Best:       frame %d (PSNR=%.2f dB)\n", best_frame, best_psnr);

    if (f_diff)
        printf("  Diff file:  %s (%d frames)\n", diff_path, frame_count);

done:
    free(buf_orig);
    free(buf_recon);
    if (f_orig) fclose(f_orig);
    if (f_recon) fclose(f_recon);
    if (f_diff) fclose(f_diff);
    return 0;
}