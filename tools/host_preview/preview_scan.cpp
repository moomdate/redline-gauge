// Host preview for the BLUETOOTH pair modal (scan_ui), with a stub ObdSource scan list.
#include <stdio.h>
#include <string.h>
#include "ui/gauge_ui.h"
#include "ui/scan_ui.h"
#include "ui/theme.h"
#include "data/obd_source.h"
#include "config.h"

static uint16_t fb[320 * 240];
static void push(int x, int y, int w, int h, const uint16_t *px) {
    for (int r = 0; r < h; r++) memcpy(&fb[(y + r) * 320 + x], px + r * w, w * 2);
}
static void save(const char *path) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 320 240 255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                           (uint8_t)((c & 31) << 3 | ((c >> 2) & 7)) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// ---- stub ObdSource scan API ----
static int g_state = 2;
static ObdSource::BtDevice g_dev[ObdSource::kMaxScan];
static int g_n = 0;
void ObdSource::requestScan() {}
int  ObdSource::scanState() { return g_state; }
int  ObdSource::scanResults(BtDevice *out, int max) { int n = g_n < max ? g_n : max; for (int i=0;i<n;i++) out[i]=g_dev[i]; return n; }
void ObdSource::selectDevice(const char *) {}

static void dev(const char *name, const char *mac, int rssi, bool obd) {
    ObdSource::BtDevice &d = g_dev[g_n++];
    strncpy(d.name, name, sizeof d.name); strncpy(d.mac, mac, sizeof d.mac); d.rssi = rssi; d.isObd = obd;
}

int main() {
    gauge_ui::begin(push, kThemes[THEME_ICE]);
    gauge_ui::setTheme(kThemes[THEME_ICE]);

    // scanning state
    g_state = 1; scan_ui::open();
    Settings st; st.theme = THEME_ICE;
    scan_ui::render(0, st);
    save("out/scan_scanning.ppm");

    // results with a few devices, one selected
    g_state = 2; g_n = 0; scan_ui::open();
    dev("OBDII", "AA:BB:CC:11:22:33", -52, true);
    dev("V-LINK", "11:22:33:44:55:66", -61, true);
    dev("(no name)", "77:88:99:AA:BB:CC", -70, false);
    dev("iPhone", "DE:AD:BE:EF:00:11", -58, false);
    dev("VEEPEAK", "01:23:45:67:89:AB", -74, true);
    dev("JBL Speaker", "02:46:8A:CE:13:57", -80, false);
    scan_ui::render(0, st);
    scan_ui::tap(90, 50 + 26);          // select row 2 (V-LINK), approx
    scan_ui::render(0, st);
    save("out/scan_list.ppm");

    // amber theme variant
    gauge_ui::setTheme(kThemes[THEME_AMBER]); st.theme = THEME_AMBER;
    scan_ui::invalidate(); scan_ui::render(0, st);
    save("out/scan_list_amber.ppm");
    printf("wrote scan previews\n");
    return 0;
}
