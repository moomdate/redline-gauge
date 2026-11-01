#pragma once
// PLACEHOLDER - replaced by tools/sign/make_record.py once the signing key exists.
// Until then this is 136 zero bytes: the firmware links and runs, but verify.py finds no mark.
#include <stdint.h>

static const uint8_t kProvData[136] = { 0 };
