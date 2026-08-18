// Wake radio, scan, wait for SCAN_DONE, release. Logs AP count + best RSSI.

#include <stdio.h>
#include <stdbool.h>
#include "wifi_manager.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_scan_sem;
static int  s_ap_count;
static int8_t s_best_rssi;

static void scan_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    if (id == WIFI_MGR_EVT_SCAN_DONE && s_scan_sem) {
        wifi_manager_ap_t aps[WIFI_MANAGER_MAX_SCAN_APS];
        s_ap_count = wifi_manager_get_scan_results(aps, WIFI_MANAGER_MAX_SCAN_APS);
        s_best_rssi = -127;
        for (int i = 0; i < s_ap_count; i++) {
            if (aps[i].rssi > s_best_rssi) s_best_rssi = aps[i].rssi;
        }
        xSemaphoreGive(s_scan_sem);
    }
}

bool hw_test_wifi_scan(char *detail, size_t n) {
    if (!s_scan_sem) s_scan_sem = xSemaphoreCreateBinary();
    s_ap_count = 0;
    s_best_rssi = -127;

    esp_event_handler_register(WIFI_MANAGER_EVENT_BASE, WIFI_MGR_EVT_SCAN_DONE,
                                scan_evt, NULL);

    bool ok = false;
    if (wifi_manager_wake() != ESP_OK) {
        snprintf(detail, n, "wake failed");
        goto unreg;
    }
    if (wifi_manager_scan() != ESP_OK) {
        snprintf(detail, n, "scan start failed");
        goto release;
    }
    if (xSemaphoreTake(s_scan_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        snprintf(detail, n, "scan timeout (5s)");
        goto release;
    }
    snprintf(detail, n, "%d AP%s, best RSSI=%d dBm",
             s_ap_count, s_ap_count == 1 ? "" : "s", (int)s_best_rssi);
    ok = true;

release:
    wifi_manager_release();
unreg:
    esp_event_handler_unregister(WIFI_MANAGER_EVENT_BASE, WIFI_MGR_EVT_SCAN_DONE,
                                  scan_evt);
    return ok;
}
