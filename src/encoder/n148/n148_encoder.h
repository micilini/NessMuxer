#ifndef NESS_N148_ENCODER_H
#define NESS_N148_ENCODER_H

#include "../encoder.h"

extern const NessEncoderVtable g_n148_encoder_vtable;

#define N148_GOP_KEYINT_DEFAULT 30

int n148_encoder_set_profile_entropy_for_tests(void* enc, int profile, int entropy_mode);

#endif