#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "../include/ness_muxer.h"

static double now_ms(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;

    if (freq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0)
            return (double)GetTickCount64();
    }

    if (!QueryPerformanceCounter(&counter))
        return (double)GetTickCount64();

    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static long long get_file_size(FILE* fp)
{
    long long size;
#if defined(_WIN32)
    _fseeki64(fp, 0, SEEK_END);
    size = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_END);
    size = (long long)ftello(fp);
    fseeko(fp, 0, SEEK_SET);
#endif
    return size;
}

static int parse_int_arg(const char* s, int* out)
{
    char* endptr = NULL;
    long v;
    if (!s || !out)
        return 0;
    v = strtol(s, &endptr, 10);
    if (endptr == s || *endptr != '\0' || v <= 0 || v > 2147483647L)
        return 0;
    *out = (int)v;
    return 1;
}

static void usage(void)
{
    printf("n148_regression_bench <input.raw> --width W --height H --fps F --bitrate K [--iterations N] [--csv out.csv] [--label name]\n");
}

int main(int argc, char** argv)
{
    const char* input_path = NULL;
    const char* csv_path = NULL;
    const char* label = "n148_cabac";
    int width = 0;
    int height = 0;
    int fps = 0;
    int bitrate = 0;
    int iterations = 3;
    int frame_size;
    FILE* input = NULL;
    uint8_t* frame_buf = NULL;
    long long input_size;
    long long total_frames;
    int iter;
    double sum_ms = 0.0;
    long long sum_bytes = 0;
    FILE* csv = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (!input_path && argv[i][0] != '-') {
            input_path = argv[i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &width)) return 1;
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &height)) return 1;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &fps)) return 1;
        } else if (strcmp(argv[i], "--bitrate") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &bitrate)) return 1;
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &iterations)) return 1;
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else {
            usage();
            return 1;
        }
    }

    if (!input_path || width <= 0 || height <= 0 || fps <= 0 || bitrate <= 0) {
        usage();
        return 1;
    }

    frame_size = width * height * 3 / 2;
    input = fopen(input_path, "rb");
    if (!input) {
        fprintf(stderr, "cannot open input: %s\n", input_path);
        return 1;
    }

    input_size = get_file_size(input);
    if (input_size <= 0 || (input_size % frame_size) != 0) {
        fprintf(stderr, "input size is not a clean NV12 multiple\n");
        fclose(input);
        return 1;
    }
    total_frames = input_size / frame_size;

    frame_buf = (uint8_t*)malloc((size_t)frame_size);
    if (!frame_buf) {
        fclose(input);
        return 1;
    }

    if (csv_path) {
        csv = fopen(csv_path, "w");
        if (!csv) {
            fprintf(stderr, "cannot create csv: %s\n", csv_path);
            free(frame_buf);
            fclose(input);
            return 1;
        }
        fprintf(csv, "label,iteration,bytes,elapsed_ms,frames,avg_kbps,guardrail_243kb_pass\n");
    }

    printf("=== N.148 regression bench ===\n");
    printf("  Input: %s\n", input_path);
    printf("  Size: %dx%d @ %dfps\n", width, height, fps);
    printf("  Frames: %lld\n", total_frames);
    printf("  Bitrate target: %d kbps\n", bitrate);
    printf("  Iterations: %d\n\n", iterations);

    for (iter = 0; iter < iterations; iter++) {
        NessMuxer* muxer = NULL;
        NessMuxerConfig cfg;
        char out_path[256];
        FILE* outf;
        long long bytes;
        double t0;
        double t1;
        long long frame_idx;

        memset(&cfg, 0, sizeof(cfg));
        snprintf(out_path, sizeof(out_path), "n148_regression_iter_%d.mkv", iter + 1);
        cfg.output_path = out_path;
        cfg.width = width;
        cfg.height = height;
        cfg.fps = fps;
        cfg.bitrate_kbps = bitrate;
        cfg.encoder_type = NESS_ENCODER_N148;
        cfg.codec_type = NESS_CODEC_N148;
        cfg.entropy_mode = NESS_ENTROPY_CABAC;

        rewind(input);
        if (ness_muxer_open(&muxer, &cfg) != 0) {
            fprintf(stderr, "ness_muxer_open failed on iteration %d\n", iter + 1);
            if (csv) fclose(csv);
            free(frame_buf);
            fclose(input);
            return 1;
        }

        t0 = now_ms();
        for (frame_idx = 0; frame_idx < total_frames; frame_idx++) {
            if (fread(frame_buf, 1, (size_t)frame_size, input) != (size_t)frame_size) {
                fprintf(stderr, "short read on frame %lld\n", frame_idx);
                ness_muxer_close(muxer);
                if (csv) fclose(csv);
                free(frame_buf);
                fclose(input);
                return 1;
            }
            if (ness_muxer_write_frame(muxer, frame_buf, frame_size) != 0) {
                fprintf(stderr, "ness_muxer_write_frame failed on frame %lld\n", frame_idx);
                ness_muxer_close(muxer);
                if (csv) fclose(csv);
                free(frame_buf);
                fclose(input);
                return 1;
            }
        }
        if (ness_muxer_close(muxer) != 0) {
            fprintf(stderr, "ness_muxer_close failed on iteration %d\n", iter + 1);
            if (csv) fclose(csv);
            free(frame_buf);
            fclose(input);
            return 1;
        }
        t1 = now_ms();

        outf = fopen(out_path, "rb");
        if (!outf) {
            fprintf(stderr, "cannot reopen output: %s\n", out_path);
            if (csv) fclose(csv);
            free(frame_buf);
            fclose(input);
            return 1;
        }
        bytes = get_file_size(outf);
        fclose(outf);

        sum_ms += (t1 - t0);
        sum_bytes += bytes;

        printf("  Iteration %d: %lld bytes | %.2f ms\n", iter + 1, bytes, t1 - t0);
        if (csv) {
            double avg_kbps = ((double)bytes * 8.0 * (double)fps) / ((double)total_frames * 1000.0);
            fprintf(csv, "%s,%d,%lld,%.3f,%lld,%.3f,%s\n",
                    label,
                    iter + 1,
                    bytes,
                    t1 - t0,
                    total_frames,
                    avg_kbps,
                    bytes <= (243LL * 1024LL) ? "yes" : "no");
        }
    }

    printf("\n  Average bytes: %.2f\n", (double)sum_bytes / (double)iterations);
    printf("  Average time:  %.2f ms\n", sum_ms / (double)iterations);
    printf("  Guardrail 243KB: %s\n", ((sum_bytes / iterations) <= (243LL * 1024LL)) ? "PASS" : "FAIL");

    if (csv)
        fclose(csv);
    free(frame_buf);
    fclose(input);
    return 0;
}
