#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../include/ness_muxer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct
{
    int width;
    int height;
    int bitrate_kbps;
} BenchPreset;

static void print_usage(void)
{
    printf("NessMuxer Encoder Benchmark\n");
    printf("\n");
    printf("Usage:\n");
    printf("  nessmux_bench [--encoder auto|mf|x264|nvenc|n148]\n");
    printf("  nessmux_bench --width <w> --height <h> --frames <n> [--encoder auto|mf|x264|nvenc]\n");
    printf("\n");
}

static int parse_int_arg(const char* value, int* out_value)
{
    char* endptr = NULL;
    long v;

    if (!value || !out_value)
        return 0;

    v = strtol(value, &endptr, 10);
    if (endptr == value || *endptr != '\0')
        return 0;
    if (v <= 0 || v > 2147483647L)
        return 0;

    *out_value = (int)v;
    return 1;
}

static int parse_encoder_arg(const char* value, int* out_encoder_type)
{
    if (!value || !out_encoder_type)
        return 0;

    if (strcmp(value, "auto") == 0) {
        *out_encoder_type = NESS_ENCODER_AUTO;
        return 1;
    }
    if (strcmp(value, "mf") == 0 || strcmp(value, "mediafoundation") == 0) {
        *out_encoder_type = NESS_ENCODER_MEDIA_FOUNDATION;
        return 1;
    }
    if (strcmp(value, "x264") == 0) {
        *out_encoder_type = NESS_ENCODER_X264;
        return 1;
    }
    if (strcmp(value, "nvenc") == 0) {
        *out_encoder_type = NESS_ENCODER_NVENC;
        return 1;
    }
    if (strcmp(value, "videotoolbox") == 0 || strcmp(value, "vt") == 0) {
        *out_encoder_type = NESS_ENCODER_VIDEOTOOLBOX;
        return 1;
    }
    if (strcmp(value, "n148") == 0) {
        *out_encoder_type = NESS_ENCODER_N148;
        return 1;
    }

    return 0;
}

static double now_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;

    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);

    QueryPerformanceCounter(&counter);
    return ((double)counter.QuadPart * 1000.0) / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
#endif
}

static long long get_file_size_path(const char* path)
{
    FILE* fp;
    long long size;

    fp = fopen(path, "rb");
    if (!fp)
        return -1;

#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    size = _ftelli64(fp);
#else
    if (fseeko(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    size = (long long)ftello(fp);
#endif

    fclose(fp);
    return size;
}

static void fill_nv12_pattern(uint8_t* nv12, int width, int height, int frame_index)
{
    int x, y;
    uint8_t* y_plane = nv12;
    uint8_t* uv_plane = nv12 + (width * height);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            y_plane[y * width + x] = (uint8_t)((x + y + frame_index * 3) & 0xFF);
        }
    }

    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width; x += 2) {
            uv_plane[y * width + x + 0] = (uint8_t)((128 + frame_index * 2 + x / 2) & 0xFF);
            uv_plane[y * width + x + 1] = (uint8_t)((64 + frame_index * 3 + y) & 0xFF);
        }
    }
}

static int run_single_bench(int width, int height, int fps, int bitrate_kbps, int frames, int encoder_type)
{
    int frame_size;
    uint8_t* frame_buf = NULL;
    char output_name[128];
    NessMuxerConfig config;
    NessMuxer* muxer = NULL;
    double t0, t1, elapsed_ms, fps_result;
    long long file_size;
    int i;
    int rc = 1;

    if ((width % 2) != 0 || (height % 2) != 0) {
        printf("ERROR: resolution must be even (%dx%d)\n", width, height);
        return 1;
    }

    frame_size = width * height * 3 / 2;
    frame_buf = (uint8_t*)malloc((size_t)frame_size);
    if (!frame_buf) {
        printf("ERROR: failed to allocate %d bytes\n", frame_size);
        return 1;
    }

#if defined(_WIN32)
    _snprintf(output_name, sizeof(output_name), "bench_%dx%d.mkv", width, height);
#else
    snprintf(output_name, sizeof(output_name), "bench_%dx%d.mkv", width, height);
#endif
    output_name[sizeof(output_name) - 1] = '\0';

    memset(&config, 0, sizeof(config));
    config.output_path = output_name;
    config.width = width;
    config.height = height;
    config.fps = fps;
    config.bitrate_kbps = bitrate_kbps;
    config.encoder_type = encoder_type;
    config.codec_type = (encoder_type == NESS_ENCODER_N148) ? NESS_CODEC_N148 : NESS_CODEC_AVC;

    if (ness_muxer_open(&muxer, &config) != NESS_OK) {
        printf("ERROR: ness_muxer_open failed for %s\n", output_name);
        goto cleanup;
    }

    t0 = now_ms();

    for (i = 0; i < frames; i++) {
        fill_nv12_pattern(frame_buf, width, height, i);

        if (ness_muxer_write_frame(muxer, frame_buf, frame_size) != NESS_OK) {
            printf("ERROR: ness_muxer_write_frame failed at frame %d (%dx%d)\n", i, width, height);
            printf("       Details: %s\n", ness_muxer_error(muxer));
            goto cleanup;
        }
    }

    if (ness_muxer_close(muxer) != NESS_OK) {
        printf("WARNING: ness_muxer_close returned an error for %s\n", output_name);
    }

    t1 = now_ms();
    elapsed_ms = t1 - t0;
    fps_result = (elapsed_ms > 0.0) ? ((double)frames * 1000.0 / elapsed_ms) : 0.0;
    file_size = get_file_size_path(output_name);

    printf("%-12dx%-6d %-8d %-10.2f %-8.1f %-10dkbps %lld KB\n",
           width,
           height,
           frames,
           elapsed_ms,
           fps_result,
           bitrate_kbps,
           (file_size >= 0) ? (file_size / 1024) : -1LL);

    rc = 0;

cleanup:
    if (muxer) {
        free(muxer);
        muxer = NULL;
    }

    if (frame_buf) {
        free(frame_buf);
        frame_buf = NULL;
    }

    remove(output_name);
    return rc;
}

int main(int argc, char** argv)
{
    const int fps = 30;
    int frames = 300;
    int custom_width = 0;
    int custom_height = 0;
    int encoder_type = NESS_ENCODER_AUTO;
    int i;

    BenchPreset presets[] = {
        { 320,  240, 1000 },
        { 640,  480, 2000 },
        { 1280, 720, 4000 },
        { 1920, 1080, 6000 }
    };

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--width") == 0) {
                if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &custom_width)) {
                    printf("ERROR: invalid value for --width\n");
                    return 1;
                }
                i++;
            }
            else if (strcmp(argv[i], "--height") == 0) {
                if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &custom_height)) {
                    printf("ERROR: invalid value for --height\n");
                    return 1;
                }
                i++;
            }
            else if (strcmp(argv[i], "--frames") == 0) {
                if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &frames)) {
                    printf("ERROR: invalid value for --frames\n");
                    return 1;
                }
                i++;
            }
            else if (strcmp(argv[i], "--encoder") == 0) {
                if (i + 1 >= argc || !parse_encoder_arg(argv[i + 1], &encoder_type)) {
                    printf("ERROR: invalid value for --encoder\n");
                    return 1;
                }
                i++;
            }
            else {
                print_usage();
                return 1;
            }
        }
    }

    printf("NessMuxer Encoder Benchmark\n");
    printf("===========================\n");
    printf("Backend: %s\n",
           (encoder_type == NESS_ENCODER_MEDIA_FOUNDATION) ? "mf" :
           (encoder_type == NESS_ENCODER_X264) ? "x264" :
           (encoder_type == NESS_ENCODER_NVENC) ? "nvenc" :
           (encoder_type == NESS_ENCODER_N148) ? "n148" : "auto");
    printf("%-18s %-8s %-10s %-8s %-14s %s\n",
           "Resolution", "Frames", "Time(ms)", "FPS", "Bitrate", "File Size");

    if (custom_width > 0 || custom_height > 0) {
        int bitrate;

        if (custom_width <= 0 || custom_height <= 0) {
            printf("ERROR: custom benchmark requires both --width and --height\n");
            return 1;
        }

        bitrate = (custom_width >= 1920 || custom_height >= 1080) ? 6000 :
                  (custom_width >= 1280 || custom_height >= 720)  ? 4000 :
                  (custom_width >= 640  || custom_height >= 480)  ? 2000 : 1000;

                return run_single_bench(custom_width, custom_height, fps, bitrate, frames, encoder_type);
    }
    else {
        size_t count = sizeof(presets) / sizeof(presets[0]);
        size_t idx;

        for (idx = 0; idx < count; idx++) {
            if (run_single_bench(presets[idx].width,
                    presets[idx].height,
                    fps,
                    presets[idx].bitrate_kbps,
                    frames,
                    encoder_type) != 0) {
                return 1;
            }
        }
    }

    return 0;
}