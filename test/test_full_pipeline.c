#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ness_muxer.h"

static void generate_nv12_frame(uint8_t* buf, int width, int height, int frame_idx)
{
    int x, y;
    uint8_t* y_plane = buf;
    uint8_t* uv_plane = buf + width * height;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            y_plane[y * width + x] = (uint8_t)((x * 255 / width + frame_idx * 3) % 256);

    for (y = 0; y < height / 2; y++)
        for (x = 0; x < width / 2; x++) {
            uv_plane[y * width + x * 2]     = (uint8_t)((128 + frame_idx * 2 + y) % 256);
            uv_plane[y * width + x * 2 + 1] = (uint8_t)((128 + frame_idx * 2 + x) % 256);
        }
}

int main(void)
{
    NessMuxer* muxer = NULL;
    NessMuxerConfig config;
    int ret, i;
    int width = 320;
    int height = 240;
    int fps = 30;
    int num_frames = 90;
    int nv12_size = width * height * 3 / 2;
    uint8_t* nv12_buf = NULL;
    const char* output_path = "test_full_pipeline.mkv";

    printf("\n=== NessMuxer - Full Pipeline Test (Phase 7) ===\n\n");

    nv12_buf = (uint8_t*)malloc(nv12_size);
    if (!nv12_buf) {
        printf("FAIL: malloc\n");
        return 1;
    }

    memset(&config, 0, sizeof(config));
    config.output_path = output_path;
    config.width = width;
    config.height = height;
    config.fps = fps;
    config.bitrate_kbps = 1000;

    printf("[1] Opening muxer %dx%d @%dfps...\n", width, height, fps);
    ret = ness_muxer_open(&muxer, &config);
    if (ret != NESS_OK) {
        printf("FAIL: ness_muxer_open returned %d\n", ret);
        free(nv12_buf);
        return 1;
    }
    printf("    OK\n\n");

    printf("[2] Writing %d NV12 frames...\n", num_frames);
    for (i = 0; i < num_frames; i++) {
        generate_nv12_frame(nv12_buf, width, height, i);
        ret = ness_muxer_write_frame(muxer, nv12_buf, nv12_size);
        if (ret != NESS_OK) {
            printf("FAIL: write_frame at frame %d (ret=%d, err=%s)\n",
                   i, ret, ness_muxer_error(muxer));
            break;
        }
        if ((i + 1) % 30 == 0)
            printf("    %d/%d frames written\n", i + 1, num_frames);
    }

    printf("\n[3] Closing (drain + finalize)...\n");
    ret = ness_muxer_close(muxer);
    if (ret != NESS_OK)
        printf("    WARNING: close returned %d\n", ret);

    printf("\n[4] Results:\n");
    printf("    Frames submitted:  %lld\n", (long long)ness_muxer_frame_count(muxer));
    printf("    Packets written:   %lld\n", (long long)ness_muxer_encoded_count(muxer));

    {
        FILE* fp = fopen(output_path, "rb");
        if (fp) {
            long size;
            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fclose(fp);
            printf("    Output file:       %s (%ld bytes)\n", output_path, size);
        } else {
            printf("    Output file:       NOT CREATED\n");
        }
    }

    printf("\n[5] Checks:\n");
    {
        int ok = 1;
        int64_t fc = ness_muxer_frame_count(muxer);
        int64_t ec = ness_muxer_encoded_count(muxer);
        FILE* fp = fopen(output_path, "rb");

        if (fc == num_frames)
            printf("    [PASS] Frame count = %d\n", num_frames);
        else { printf("    [FAIL] Frame count = %lld\n", (long long)fc); ok = 0; }

        if (ec > 0)
            printf("    [PASS] Encoded packets = %lld\n", (long long)ec);
        else { printf("    [FAIL] No encoded packets\n"); ok = 0; }

        if (fp) {
            unsigned char hdr[4];
            fread(hdr, 1, 4, fp);
            fclose(fp);
            if (hdr[0]==0x1A && hdr[1]==0x45 && hdr[2]==0xDF && hdr[3]==0xA3)
                printf("    [PASS] Valid EBML header\n");
            else { printf("    [FAIL] Invalid EBML header\n"); ok = 0; }
        } else { printf("    [FAIL] Output file not found\n"); ok = 0; }

        printf("\n%s\n\n", ok ? "=== SUCCESS: Full pipeline working! ===" :
                                "=== FAILURE: Some checks failed ===");

        printf("Validate:\n");
        printf("  ffplay %s\n", output_path);
        printf("  ffprobe -v error -show_format -show_streams %s\n\n", output_path);

        free(nv12_buf);
        free(muxer);
        return ok ? 0 : 1;
    }
}
