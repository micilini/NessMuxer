
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "encoder/encoder.h"
#include "encoder/n148/n148_encoder.h"
#include "codec/n148/n148_spec.h"





typedef struct {
    FILE* out_f;
    int   pkt_count;
    int   total_bytes;
} WriteCtx;

static int on_packet(void* userdata, const NessEncodedPacket* pkt)
{
    WriteCtx* wctx = (WriteCtx*)userdata;

    if (!wctx || !pkt || !pkt->data || pkt->size <= 0)
        return 0;

    fwrite(pkt->data, 1, (size_t)pkt->size, wctx->out_f);
    wctx->pkt_count++;
    wctx->total_bytes += pkt->size;

    return 0;
}





int main(int argc, char* argv[])
{
    const char* input_path = NULL;
    const char* output_path = NULL;
    int width = 0, height = 0, fps = 30;
    int qp = 22;
    int bitrate = 2000;
    int profile = N148_PROFILE_MAIN;
    int entropy = N148_ENTROPY_CAVLC;
    int max_frames = 0;
    int i;

    FILE* f_in = NULL;
    FILE* f_out = NULL;
    void* enc = NULL;
    uint8_t* frame_buf = NULL;
    int frame_size;
    int frame_count = 0;
    WriteCtx wctx;

    const NessEncoderVtable* vtable = &g_n148_encoder_vtable;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc)
            fps = atoi(argv[++i]);
        else if (strcmp(argv[i], "--qp") == 0 && i + 1 < argc)
            qp = atoi(argv[++i]);
        else if (strcmp(argv[i], "--bitrate") == 0 && i + 1 < argc)
            bitrate = atoi(argv[++i]);
        else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "epic") == 0 || strcmp(argv[i], "Epic") == 0) {
                profile = N148_PROFILE_EPIC;
                entropy = N148_ENTROPY_CABAC;
            } else {
                profile = N148_PROFILE_MAIN;
                entropy = N148_ENTROPY_CAVLC;
            }
        }
        else if (strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc)
            max_frames = atoi(argv[++i]);
        else if (!input_path)
            input_path = argv[i];
        else if (!output_path)
            output_path = argv[i];
    }

    if (!input_path || !output_path || width <= 0 || height <= 0) {
        fprintf(stderr,
            "Usage: n148enc <input.nv12> <output.n148> "
            "--width W --height H [--fps F] [--qp Q] [--bitrate B] "
            "[--profile main|epic] [--max-frames N]\n");
        return 1;
    }

    frame_size = width * height * 3 / 2;

    printf("=== n148enc ===\n");
    printf("  Input:    %s\n", input_path);
    printf("  Output:   %s\n", output_path);
    printf("  Size:     %dx%d  fps=%d\n", width, height, fps);
    printf("  Profile:  %s  entropy=%s\n",
           profile == N148_PROFILE_EPIC ? "Epic" : "Main",
           entropy == N148_ENTROPY_CABAC ? "CABAC" : "CAVLC");
    printf("  QP:       %d  bitrate=%dk\n\n", qp, bitrate);

   
    f_in = fopen(input_path, "rb");
    if (!f_in) { fprintf(stderr, "Cannot open input: %s\n", input_path); return 1; }

    f_out = fopen(output_path, "wb");
    if (!f_out) { fprintf(stderr, "Cannot create output: %s\n", output_path); fclose(f_in); return 1; }

   
    if (vtable->create(&enc, width, height, fps, bitrate) != 0) {
        fprintf(stderr, "Encoder create failed\n");
        goto fail;
    }

   
    if (profile == N148_PROFILE_EPIC) {
        n148_encoder_set_profile_entropy_for_tests(enc, profile, entropy);
    }

   
    frame_buf = (uint8_t*)malloc((size_t)frame_size);
    if (!frame_buf) { fprintf(stderr, "Out of memory\n"); goto fail; }

    memset(&wctx, 0, sizeof(wctx));
    wctx.out_f = f_out;

   
    while (1) {
        size_t rd = fread(frame_buf, 1, (size_t)frame_size, f_in);
        if (rd < (size_t)frame_size) break;

        if (vtable->submit_frame(enc, frame_buf, frame_size) != 0) {
            fprintf(stderr, "submit_frame failed at frame %d\n", frame_count);
            break;
        }

        vtable->receive_packets(enc, on_packet, &wctx);
        frame_count++;

        if (frame_count % 100 == 0)
            printf("  %d frames encoded...\n", frame_count);

        if (max_frames > 0 && frame_count >= max_frames) break;
    }

   
    vtable->drain(enc, on_packet, &wctx);

    printf("\n  ========== Results ==========\n");
    printf("  Frames encoded:  %d\n", frame_count);
    printf("  Packets output:  %d\n", wctx.pkt_count);
    printf("  Total bytes:     %d\n", wctx.total_bytes);
    if (frame_count > 0) {
        printf("  Avg bytes/frame: %d\n", wctx.total_bytes / frame_count);
        printf("  Avg bitrate:     %.1f kbps\n",
               (double)wctx.total_bytes * 8.0 * fps / (frame_count * 1000.0));
    }
    printf("  Output file:     %s\n", output_path);

    vtable->destroy(enc);
    enc = NULL;
    free(frame_buf);
    fclose(f_in);
    fclose(f_out);
    return 0;

fail:
    if (enc) vtable->destroy(enc);
    free(frame_buf);
    if (f_in) fclose(f_in);
    if (f_out) fclose(f_out);
    return 1;
}