// Probe each expected I2C peripheral. ESP-IDF 5.x new-API probe.

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"

typedef struct { const char *name; uint8_t addr; } probe_t;

static const probe_t s_devs[] = {
    { "AXP2101",  0x34 },
    { "PCF85063", 0x51 },
    { "FT5x06",   0x38 },
    { "ES8311",   0x18 },
    { "QMI8658",  0x6B },
};
static const int s_n = (int)(sizeof(s_devs) / sizeof(s_devs[0]));

bool hw_test_i2c_scan(char *detail, size_t n) {
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) { snprintf(detail, n, "no I2C bus handle"); return false; }

    int found = 0;
    char missing[64] = "";
    for (int i = 0; i < s_n; i++) {
        if (i2c_master_probe(bus, s_devs[i].addr, 50) == ESP_OK) {
            found++;
        } else {
            size_t len = strlen(missing);
            snprintf(missing + len, sizeof(missing) - len, "%s%s",
                     len ? "," : "", s_devs[i].name);
        }
    }
    if (found == s_n) {
        snprintf(detail, n, "%d/%d devices ACKed", found, s_n);
    } else {
        snprintf(detail, n, "%d/%d ACKed; missing: %s", found, s_n, missing);
    }
    return found == s_n;
}
