#include "core/provenance.h"
#include "core/shade_lut.h"

namespace provenance {

// Reading every byte at runtime keeps kShadeLut in the image under --gc-sections: it is really
// used, not just defined. FNV-1a, matching tools/sign/make_record.py.
uint32_t mark() {
    uint32_t sum = 2166136261u;
    for (uint8_t b : kShadeLut) { sum ^= b; sum *= 16777619u; }
    return sum;
}

// Stripping or altering the table changes mark(), so intact() goes false and the dash shows
// UNOFFICIAL (see splash_ui::authentic). No effect on rpm / speed / any reading.
bool intact() { return SHADE_LUT_SUM != 0u && mark() == SHADE_LUT_SUM; }

}  // namespace provenance
