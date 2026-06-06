#include <stdio.h>
#include <stdbool.h>
#include "bsp/esp32_s3_touch_amoled_2_06.h"

bool hw_test_axp(char *detail, size_t n) {
    int  pct  = bsp_power_get_battery_percent();
    bool vbus = bsp_power_is_vbus_in();
    bool chg  = bsp_power_is_charging();
    snprintf(detail, n, "Batt=%d%%, VBUS=%s, Charging=%s",
             pct, vbus ? "on" : "off", chg ? "yes" : "no");
    return pct >= 0 && pct <= 100;
}
