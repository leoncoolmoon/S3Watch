// Round-trip a known i32 through a temp NVS namespace, then erase the key.

#include <stdio.h>
#include <stdbool.h>
#include "nvs_flash.h"
#include "nvs.h"

#define NS  "ondev_test"
#define KEY "probe"

bool hw_test_nvs(char *detail, size_t n) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { snprintf(detail, n, "open err=0x%x", err); return false; }

    int32_t want = 0x5A5A1234;
    err = nvs_set_i32(h, KEY, want);
    if (err != ESP_OK) { snprintf(detail, n, "set err=0x%x", err); nvs_close(h); return false; }
    err = nvs_commit(h);
    if (err != ESP_OK) { snprintf(detail, n, "commit err=0x%x", err); nvs_close(h); return false; }

    int32_t got = 0;
    err = nvs_get_i32(h, KEY, &got);
    if (err != ESP_OK) { snprintf(detail, n, "get err=0x%x", err); nvs_close(h); return false; }

    (void)nvs_erase_key(h, KEY);
    (void)nvs_commit(h);
    nvs_close(h);

    if (got != want) {
        snprintf(detail, n, "mismatch: got 0x%08x", (unsigned)got);
        return false;
    }
    snprintf(detail, n, "round-trip OK");
    return true;
}
