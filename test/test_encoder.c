

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mf_encoder.h"


static int g_packet_count = 0;
static int g_keyframe_count = 0;
static int g_total_bytes = 0;
static FILE* g_output_file = NULL;


static int on_packet(void* userdata, const MFEncodedPacket* pkt)
{
    (void)userdata;

    g_packet_count++;
    g_total_bytes += pkt->size;

    if (pkt->is_keyframe)
        g_keyframe_count++;

    printf("    Packet #%d: size=%d, pts=%lld, keyframe=%d\n",
           g_packet_count, pkt->size,
           (long long)pkt->pts_hns, pkt->is_keyframe);

   
    if (g_output_file && pkt->data && pkt->size > 0) {
        fwrite(pkt->data, 1, pkt->size, g_output_file);
    }

    return 0;
}


static void generate_nv12_frame(uint8_t* buf, int width, int height, int frame_idx)
{
    int x, y;
    uint8_t* y_plane = buf;
    uint8_t* uv_plane = buf + width * height;

   
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int val = (x * 255 / width + frame_idx * 3) % 256;
            y_plane[y * width + x] = (uint8_t)val;
        }
    }

   
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            int u = (128 + frame_idx * 2 + y) % 256;
            int v = (128 + frame_idx * 2 + x) % 256;
            uv_plane[y * width + x * 2]     = (uint8_t)u;
            uv_plane[y * width + x * 2 + 1] = (uint8_t)v;
        }
    }
}

int main(void)
{
    MFEncoder* enc = NULL;
    int ret;
    int width = 320;
    int height = 240;
    int fps = 30;
    int bitrate_kbps = 1000;
    int num_frames = 90; 
    int nv12_size = width * height * 3 / 2;
    uint8_t* nv12_buf = NULL;
    int i;
    uint8_t* codec_private = NULL;
    int codec_private_size = 0;

    printf("\n=== NessMuxer - Teste Fase 3 (Encoder MF) ===\n\n");

   
    nv12_buf = (uint8_t*)malloc(nv12_size);
    if (!nv12_buf) {
        printf("ERRO: malloc para NV12 falhou\n");
        return 1;
    }

   
    printf("[1] Criando encoder %dx%d @%dfps, %dkbps...\n", width, height, fps, bitrate_kbps);
    ret = mf_encoder_create(&enc, width, height, fps, bitrate_kbps);
    if (ret != 0) {
        printf("ERRO: mf_encoder_create falhou (ret=%d)\n", ret);
        printf("      Certifique-se de estar rodando no Windows com Media Foundation.\n");
        free(nv12_buf);
        return 1;
    }
    printf("    Encoder criado com sucesso!\n\n");

   
    g_output_file = fopen("test_encoder.h264", "wb");
    if (!g_output_file) {
        printf("ERRO: nao foi possivel abrir test_encoder.h264\n");
        mf_encoder_destroy(enc);
        free(nv12_buf);
        return 1;
    }

   
    printf("[2] Submetendo %d frames NV12...\n", num_frames);
    for (i = 0; i < num_frames; i++) {
        generate_nv12_frame(nv12_buf, width, height, i);

        ret = mf_encoder_submit_frame(enc, nv12_buf, nv12_size);
        if (ret != 0) {
            printf("ERRO: mf_encoder_submit_frame falhou no frame %d\n", i);
            break;
        }

       
        ret = mf_encoder_receive_packets(enc, on_packet, NULL);
        if (ret < 0) {
            printf("ERRO: mf_encoder_receive_packets falhou no frame %d\n", i);
            break;
        }
    }

    printf("\n[3] Frames submetidos: %d\n", i);
    printf("    Pacotes coletados ate agora: %d\n\n", g_packet_count);

   
    printf("[4] Fazendo drain do encoder...\n");
    ret = mf_encoder_drain(enc, on_packet, NULL);
    if (ret < 0) {
        printf("AVISO: drain retornou erro (ret=%d)\n", ret);
    }

   
    printf("\n[5] Resultados:\n");
    printf("    Frames submetidos:   %d\n", num_frames);
    printf("    Pacotes H.264:       %d\n", g_packet_count);
    printf("    Keyframes:           %d\n", g_keyframe_count);
    printf("    Bytes totais:        %d\n", g_total_bytes);

   
    ret = mf_encoder_get_codec_private(enc, &codec_private, &codec_private_size);
    if (ret == 0 && codec_private_size > 0) {
        printf("    Codec Private:       %d bytes (SPS/PPS OK)\n", codec_private_size);
        printf("      AVCC version:      %d\n", codec_private[0]);
        printf("      Profile:           %d\n", codec_private[1]);
        printf("      Level:             %d\n", codec_private[3]);
    } else {
        printf("    Codec Private:       NAO DISPONIVEL\n");
    }

   
    printf("\n[6] Verificacoes:\n");

    if (g_packet_count > 0)
        printf("    [PASS] Pelo menos 1 pacote H.264 produzido\n");
    else
        printf("    [FAIL] Nenhum pacote H.264 produzido\n");

    if (g_keyframe_count > 0)
        printf("    [PASS] Pelo menos 1 keyframe (IDR)\n");
    else
        printf("    [FAIL] Nenhum keyframe produzido\n");

    if (codec_private_size > 0)
        printf("    [PASS] SPS/PPS extraidos com sucesso\n");
    else
        printf("    [FAIL] SPS/PPS nao foram extraidos\n");

    if (g_total_bytes > 0)
        printf("    [PASS] Dados H.264 nao-vazios (%d bytes)\n", g_total_bytes);
    else
        printf("    [FAIL] Nenhum dado H.264 produzido\n");

    printf("\n[7] Arquivo salvo: test_encoder.h264 (%d bytes)\n", g_total_bytes);
    printf("    Para validar: ffplay test_encoder.h264\n\n");

   
    if (g_output_file)
        fclose(g_output_file);

    mf_encoder_destroy(enc);
    free(nv12_buf);

    if (g_packet_count > 0 && g_keyframe_count > 0 && codec_private_size > 0) {
        printf("=== SUCESSO: Encoder Fase 3 funcionando! ===\n\n");
        return 0;
    } else {
        printf("=== FALHA: Alguns testes nao passaram ===\n\n");
        return 1;
    }
}
