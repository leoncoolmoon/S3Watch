# boot_manager API

### `boot_manager_run`
```c
void boot_manager_run(void);
```
Run the complete staged power-on sequence (see README.md) and return once the
watchface is live. Call exactly once, from `app_main()`, on the main task —
nothing else may initialise components before it. Blocking: returns after the
splash→watchface handoff (~2 s with the boot tone, ~1.2 s without).

Internals worth knowing:
- The tone-completion and UI-ready waits each have a 10 s safety cap
  (`BOOT_HANDOFF_CAP_MS`) — a wedged tone task degrades to a logged warning,
  never a boot stuck at the splash.
- The splash minimum (`BOOT_SPLASH_MIN_US`, 1.2 s) uses the monotonic
  `esp_timer` clock measured from when the splash became visible.
- The cJSON→PSRAM allocator hooks are installed here (stage 0) and apply
  process-wide for the lifetime of the firmware.
