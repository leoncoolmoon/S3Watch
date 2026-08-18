// Round-trip a 256-byte pattern through SPIFFS, then delete.

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define PATH "/spiffs/_ondev_test.bin"
#define SIZE 256

bool hw_test_spiffs(char *detail, size_t n) {
    uint8_t want[SIZE];
    for (int i = 0; i < SIZE; i++) want[i] = (uint8_t)(i ^ 0xA5);

    FILE *f = fopen(PATH, "wb");
    if (!f) { snprintf(detail, n, "fopen w failed"); return false; }
    size_t w = fwrite(want, 1, SIZE, f);
    fclose(f);
    if (w != SIZE) { snprintf(detail, n, "short write %u/%u", (unsigned)w, SIZE); remove(PATH); return false; }

    uint8_t got[SIZE];
    f = fopen(PATH, "rb");
    if (!f) { snprintf(detail, n, "fopen r failed"); remove(PATH); return false; }
    size_t r = fread(got, 1, SIZE, f);
    fclose(f);
    remove(PATH);

    if (r != SIZE || memcmp(want, got, SIZE) != 0) {
        snprintf(detail, n, "compare mismatch (read %u)", (unsigned)r);
        return false;
    }
    snprintf(detail, n, "%u-byte round-trip OK", SIZE);
    return true;
}
