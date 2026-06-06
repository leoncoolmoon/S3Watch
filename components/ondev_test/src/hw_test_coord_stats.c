// Always passes; dumps task_coordinator subscriber stats to serial.
// Useful for spotting subscribers that are silently consuming CPU.

#include <stdio.h>
#include <stdbool.h>
#include "task_coordinator.h"

bool hw_test_coord_stats(char *detail, size_t n) {
    task_coord_dump_stats();
    snprintf(detail, n, "dumped to log");
    return true;
}
