// app_main is a thin shim: the entire power-on sequence (core HW → boot
// splash + tone → services → UI build → splash→watchface handoff) lives in
// the boot_manager component. When boot_manager_run() returns the watch is in
// normal operation and this task exits.

#include "boot_manager.h"

extern "C" void app_main(void)
{
    boot_manager_run();
}
