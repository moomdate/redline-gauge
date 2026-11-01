#pragma once
// Build-provenance mark, disguised as a shading look-up table (src/core/shade_lut.h, generated
// by tools/sign/make_record.py). A signed record is compiled into the firmware so an image built
// from this core - even with a replaced UI - can be proven to descend from the owner's build
// (off-device, tools/sign/verify.py). On-device it only gates the "official" badge; it never
// changes any value the gauge shows, because a dashboard must stay predictable.
#include <stdint.h>

namespace provenance {
uint32_t mark();     // checksum over the table; also keeps it referenced so the linker can't drop it
bool     intact();   // true when the table matches the value baked in at build time
}
