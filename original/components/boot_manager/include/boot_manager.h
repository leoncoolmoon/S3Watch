#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// boot_manager — owns the entire power-on sequence:
//
//   core HW → display + boot splash → settings + boot tone → services →
//   UI build (behind the splash) → splash→watchface handoff → return.
//
// Call once from app_main(); when it returns, the watch is in normal
// operation and app_main can exit. See README.md for the stage breakdown.
void boot_manager_run(void);

#ifdef __cplusplus
}
#endif
