#pragma once
// ============================================================================
//  Feeding the gauge with your own values — the easy way.
//
//  1. Edit src/my_sensors.cpp: fill in mySensorsRead() (examples inside).
//  2. On the device: SETUP -> DATA SOURCE -> CUSTOM   (or serial: mode=custom)
//
//  mySensorsRead() is called every CUSTOM_READ_MS. Set only the fields you
//  have; anything left as NAN is simply not shown ("--").
//
//  Somewhere else in your code (a CAN callback, a BLE notify, …)?  Call
//  gauge::set(CH_RPM, value) from anywhere, any task — it is thread-safe.
// ============================================================================
#include <Arduino.h>
#include <math.h>
#include "data/gauge_bus.h"

#define CUSTOM_READ_MS 20          // 50 Hz

struct GaugeInput {
    float rpm      = NAN;   // rev/min
    float speed    = NAN;   // km/h
    float coolant  = NAN;   // °C
    float voltage  = NAN;   // V
    float intake   = NAN;   // °C
    float gear     = NAN;   // 0 = N, 1..n. Leave NAN to let the gauge estimate it
    float throttle = NAN;   // %
    float fuelLevel = NAN;  // %
    float fuelRate  = NAN;  // L/h
    float stft      = NAN;  // %
    float ltft      = NAN;  // %
    float map       = NAN;  // kPa
    float ignition  = NAN;  // deg
    float o2Mv      = NAN;  // mV
};

void mySensorsBegin();                 // once, when CUSTOM becomes active
void mySensorsRead(GaugeInput &in);    // every CUSTOM_READ_MS

namespace gauge {
inline void set(Channel ch, float v) { bus::publish(ch, v); }
}

// ---- helpers for common car signals ----------------------------------------------------

// Voltage through a divider:  car 12V --R1--+--R2-- GND, junction to an ADC1 pin (GPIO 35).
// e.g. R1 = 47k, R2 = 10k  -> 0..18 V maps to 0..3.1 V.
inline float readDividerVolts(int pin, float r1, float r2) {
    return analogReadMilliVolts(pin) / 1000.0f * (r1 + r2) / r2;
}

// NTC thermistor to GND with a pull-up rSeries to 3.3 V (typical coolant/IAT sender).
// r25 = resistance at 25 °C, beta from the datasheet (≈3950 for many senders).
inline float readNtcCelsius(int pin, float rSeries, float r25, float beta) {
    float v = analogReadMilliVolts(pin) / 1000.0f;
    // The ADC tops out near 3.1 V, so an unplugged sender (pin pulled to 3.3 V) reads ~3.1 V:
    // treat >= 3.05 V as open. That still covers ~-20 degC with a 2.2k pull-up / 2.5k NTC.
    if (v <= 0.01f || v >= 3.05f) return NAN;               // open / short: no reading
    float r = rSeries * v / (3.3f - v);
    return 1.0f / (logf(r / r25) / beta + 1.0f / 298.15f) - 273.15f;
}

// Frequency input: ignition / tach signal (via opto-isolator!) or a VSS speed sensor.
// value() = pulses per second * scale, e.g. scale = 60 / pulsesPerRev for RPM
// (4-cyl wasted spark: 2 pulses per rev -> scale 30).
class PulseInput {
public:
    void begin(int pin, float scale, uint32_t timeoutMs = 500) {
        scale_ = scale; timeout_ = timeoutMs * 1000;
        pinMode(pin, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(pin), isr, this, FALLING);
    }
    float value() {
        noInterrupts();
        uint32_t period = period_, last = last_;
        interrupts();
        if (!period || micros() - last > timeout_) return 0;     // stopped
        return 1e6f / period * scale_;
    }
private:
    volatile uint32_t last_ = 0, period_ = 0;
    uint32_t timeout_ = 500000;
    float scale_ = 1;
    // Defined in gauge_input.cpp: an IRAM_ATTR function defined inline in a header
    // fails to link on Xtensa ("dangerous relocation: l32r: literal placed after use").
    static void IRAM_ATTR isr(void *arg);
};
