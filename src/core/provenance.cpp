#include "core/provenance.h"
#include "core/prov_data.h"
#include <Arduino.h>

namespace provenance {

// Reading every byte at runtime is what keeps kProvData in the image under -ffunction-sections
// / --gc-sections: it is genuinely used, not just defined. No effect on anything the gauge shows.
uint32_t mark() {
    uint32_t sum = 2166136261u;                 // FNV-1a over the mark bytes
    for (uint8_t b : kProvData) { sum ^= b; sum *= 16777619u; }
    return sum;
}

void logBoot() {
    Serial.printf("[prov] mark %u bytes, id %08x\n", (unsigned)sizeof(kProvData), (unsigned)mark());
}

}  // namespace provenance
