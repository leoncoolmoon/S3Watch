#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include "imu_manager.h"

bool hw_test_imu(char *detail, size_t n) {
    float x = 0, y = 0, z = 0;
    esp_err_t err = imu_manager_test_read_accel_g(&x, &y, &z);
    if (err != ESP_OK) {
        snprintf(detail, n, "read err=0x%x", err);
        return false;
    }
    double mag = sqrt((double)x*x + (double)y*y + (double)z*z);
    snprintf(detail, n, "x=%.2f y=%.2f z=%.2f |a|=%.2fg", x, y, z, mag);
    return mag >= 0.7 && mag <= 1.3;
}
