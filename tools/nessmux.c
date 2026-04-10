#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../include/ness_muxer.h"

static void print_usage(void)
{
    printf("NessMuxer CLI - Raw NV12 to MKV converter\n");
    printf("\n");
    printf("Usage:\n");
    printf("  nessmux <input.raw> <output.mkv> --width <w> --height <h> --fps <fps> --bitrate <kbps> [--encoder auto|mf|x264|nvenc|n148] [--codec avc|n148] [--entropy cavlc|cabac]\n");
    printf("\n");
    printf("Example:\n");
    printf("  nessmux test_input.raw test_output.mkv --width 320 --height 240 --fps 30 --bitrate 1000 --encoder n148 --codec n148 --entropy cabac\n");
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

static long long get_file_size(FILE* fp)
{
    long long size;

    if (!fp)
        return -1;

#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0)
        return -1;
    size = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
#else
    if (fseeko(fp, 0, SEEK_END) != 0)
        return -1;
    size = (long long)ftello(fp);
    fseeko(fp, 0, SEEK_SET);
#endif

    return size;
}

int main(int argc, char** argv)
{
    const char* input_path;
    const char* output_path;
    int width = 0;
    int height = 0;
    int fps = 0;
    int bitrate_kbps = 0;
    int encoder_type = NESS_ENCODER_AUTO;
    int codec_type = NESS_CODEC_AVC;
    int entropy_mode = 0;
    int frame_size;
    uint8_t* frame_buf = NULL;
    FILE* input = NULL;
    FILE* output = NULL;
    long long input_size = 0;
    long long output_size = 0;
    long long total_frames_expected = 0;
    int frames_written = 0;
    int ret = 1;
    NessMuxer* muxer = NULL;
    NessMuxerConfig config;
    int i;

    if (argc < 11) {
        print_usage();
        return 1;
    }

    input_path = argv[1];
    output_path = argv[2];

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0) {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &width)) {
                printf("ERROR: invalid value for --width\n");
                return 1;
            }
            i++;
        }
        else if (strcmp(argv[i], "--height") == 0) {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &height)) {
                printf("ERROR: invalid value for --height\n");
                return 1;
            }
            i++;
        }
        else if (strcmp(argv[i], "--fps") == 0) {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &fps)) {
                printf("ERROR: invalid value for --fps\n");
                return 1;
            }
            i++;
        }
        else if (strcmp(argv[i], "--bitrate") == 0) {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &bitrate_kbps)) {
                printf("ERROR: invalid value for --bitrate\n");
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
        }else if (strcmp(argv[i], "--codec") == 0) {
            if (i + 1 >= argc) {
                printf("ERROR: missing value for --codec\n");
                return 1;
            }
            i++;
            if (strcmp(argv[i], "avc") == 0 || strcmp(argv[i], "h264") == 0) {
                codec_type = NESS_CODEC_AVC;
            } else if (strcmp(argv[i], "n148") == 0) {
                codec_type = NESS_CODEC_N148;
            } else {
                printf("ERROR: invalid value for --codec: %s (use avc or n148)\n", argv[i]);
                return 1;
            }
        }else if (strcmp(argv[i], "--entropy") == 0) {
            if (i + 1 >= argc) {
                printf("ERROR: missing value for --entropy\n");
                return 1;
            }
            i++;
            if (strcmp(argv[i], "cavlc") == 0) {
                entropy_mode = 0;
            } else if (strcmp(argv[i], "cabac") == 0) {
                entropy_mode = 1;
            } else {
                printf("ERROR: invalid value for --entropy: %s (use cavlc or cabac)\n", argv[i]);
                return 1;
            }
        }else {
            printf("ERROR: unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (width <= 0 || height <= 0 || fps <= 0 || bitrate_kbps <= 0) {
        printf("ERROR: missing required arguments\n");
        print_usage();
        return 1;
    }

    if ((width % 2) != 0 || (height % 2) != 0) {
        printf("ERROR: width and height must be even for NV12\n");
        return 1;
    }

    frame_size = width * height * 3 / 2;

    input = fopen(input_path, "rb");
    if (!input) {
        printf("ERROR: failed to open input file: %s\n", input_path);
        goto cleanup;
    }

    input_size = get_file_size(input);
    if (input_size < 0) {
        printf("ERROR: failed to read input file size\n");
        goto cleanup;
    }

    if (input_size == 0) {
        printf("ERROR: input file is empty\n");
        goto cleanup;
    }

    if ((input_size % frame_size) != 0) {
        printf("ERROR: input size (%lld bytes) is not a multiple of frame size (%d bytes)\n",
               input_size, frame_size);
        printf("       Check width/height or make sure the file is raw NV12.\n");
        goto cleanup;
    }

    total_frames_expected = input_size / frame_size;

    frame_buf = (uint8_t*)malloc((size_t)frame_size);
    if (!frame_buf) {
        printf("ERROR: failed to allocate %d bytes for frame buffer\n", frame_size);
        goto cleanup;
    }

    memset(&config, 0, sizeof(config));
    config.output_path = output_path;
    config.width = width;
    config.height = height;
    config.fps = fps;
    config.bitrate_kbps = bitrate_kbps;
    config.encoder_type = encoder_type;
    config.codec_type = codec_type;
    config.entropy_mode = entropy_mode;

    printf("NessMuxer CLI - starting conversion\n");
    printf("  Input:        %s\n", input_path);
    printf("  Output:       %s\n", output_path);
    printf("  Resolution:   %dx%d\n", width, height);
    printf("  FPS:          %d\n", fps);
    printf("  Bitrate:      %d kbps\n", bitrate_kbps);
    printf("  Encoder:      %s\n",
           (encoder_type == NESS_ENCODER_MEDIA_FOUNDATION) ? "mf" :
           (encoder_type == NESS_ENCODER_X264) ? "x264" :
           (encoder_type == NESS_ENCODER_NVENC) ? "nvenc" :
           (encoder_type == NESS_ENCODER_N148) ? "n148" : "auto");
    printf("  Codec:        %s\n",
           (codec_type == NESS_CODEC_N148) ? "N.148" : "AVC/H.264");
    printf("  Entropy:      %s\n",
           (entropy_mode == 1) ? "CABAC" : "CAVLC");
    printf("  Frame size:   %d bytes\n", frame_size);
    printf("  Total frames: %lld\n", total_frames_expected);
    printf("\n");

    if (ness_muxer_open(&muxer, &config) != NESS_OK) {
        printf("ERROR: ness_muxer_open failed\n");
        goto cleanup;
    }

    while (1) {
        size_t read_bytes = fread(frame_buf, 1, (size_t)frame_size, input);

        if (read_bytes == 0) {
            if (feof(input))
                break;

            printf("ERROR: failed while reading input file\n");
            goto cleanup;
        }

        if ((int)read_bytes != frame_size) {
            printf("ERROR: partial frame read (%zu of %d bytes)\n", read_bytes, frame_size);
            goto cleanup;
        }

        if (ness_muxer_write_frame(muxer, frame_buf, frame_size) != NESS_OK) {
            printf("ERROR: ness_muxer_write_frame failed at frame %d\n", frames_written);
            printf("       Details: %s\n", ness_muxer_error(muxer));
            goto cleanup;
        }

        frames_written++;

        if ((frames_written % 30) == 0 || frames_written == total_frames_expected) {
            printf("  Progress: %d / %lld frames\n", frames_written, total_frames_expected);
        }
    }

    if (ness_muxer_close(muxer) != NESS_OK) {
        printf("WARNING: ness_muxer_close returned error\n");
    }

    output = fopen(output_path, "rb");
    if (output) {
        output_size = get_file_size(output);
        fclose(output);
        output = NULL;
    }

    printf("\nConversion finished successfully.\n");
    printf("  Frames submitted: %lld\n", (long long)ness_muxer_frame_count(muxer));
    printf("  Packets written:  %lld\n", (long long)ness_muxer_encoded_count(muxer));
    printf("  Duration:         %.2f s\n", (fps > 0) ? ((double)frames_written / (double)fps) : 0.0);
    if (output_size >= 0)
        printf("  Output size:      %lld bytes\n", output_size);

    ret = 0;

cleanup:
    if (output)
        fclose(output);

    if (input)
        fclose(input);

    if (frame_buf)
        free(frame_buf);

    if (muxer) {
        free(muxer);
        muxer = NULL;
    }

    return ret;
}