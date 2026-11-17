#ifndef NESS_LEGACY_V2_BRIDGE_H
#define NESS_LEGACY_V2_BRIDGE_H
#include "ness_muxer.h"
#ifdef __cplusplus
extern "C" {
#endif
int ness_legacy_v2_create(void **, const NessMuxerConfig *, char *, size_t);
int ness_legacy_v2_write(void *, const uint8_t *, int, int64_t, int, char *, size_t);
int ness_legacy_v2_close(void *, int64_t *, int64_t *, char *, size_t);
void ness_legacy_v2_counts(void *, int64_t *, int64_t *);
void ness_legacy_v2_destroy(void *);
#ifdef __cplusplus
}
#endif
#endif
