# settings

NVS-persisted user preferences. Single source of truth for all user-configurable values; every component that needs a preference reads it from here rather than maintaining its own storage.

## Storage

Settings are serialised to a JSON file on SPIFFS (`/spiffs/settings.json`). NVS is not used — the SPIFFS JSON approach allows easy inspection and backup to SD card.

## Backup / restore

`settings_backup_to_sd()` writes a snapshot to the SD card root. `settings_restore_from_sd()` applies it field-by-field — missing fields keep their current value, so a partial or older backup is safe to restore. The backup also includes the WiFi network list (exported via `wifi_manager_export_networks()`).

## Timezone

`settings_set_tz()` immediately calls `setenv("TZ", ...) + tzset()` so `localtime_r()` picks up the new value without a reboot. The TZ is stored as a POSIX TZ string (e.g. `"EST5EDT,M3.2.0/2,M11.1.0/2"`).

## Initialization

```c
settings_init();   // after bsp_display_start(), before UI / subsystem inits
```

`settings_init()` loads from SPIFFS and applies the TZ immediately. If no settings file exists, all defaults are applied and saved.

## Adding a new setting

1. **Static + default** in `settings.c`:
   ```c
   #define DEFAULT_FOO 42
   static int s_foo = DEFAULT_FOO;
   ```
2. **Getter / setter** in `settings.c` and declared in `settings.h`. The setter must call `schedule_save()` to trigger the debounced 10-second write:
   ```c
   void settings_set_foo(int val) { s_foo = clamp(val, 0, 100); schedule_save(); }
   int  settings_get_foo(void)    { return s_foo; }
   ```
3. **Write path** — add to `settings_to_json()`:
   ```c
   cJSON_AddNumberToObject(root, "foo", s_foo);
   ```
4. **Read path** — add to `settings_from_json()`, always clamping before narrowing to avoid silent wrapping on corrupt data:
   ```c
   j = cJSON_GetObjectItem(root, "foo");
   if (cJSON_IsNumber(j)) { double v = j->valuedouble; s_foo = (v < 0) ? 0 : (v > 100) ? 100 : (int)v; }
   ```
   Missing keys are silently skipped, so old backups stay safe.

The JSON key string is the field's persisted identity — never rename it after shipping.

## Dependencies

`settings` has no component-level dependencies beyond ESP-IDF base. It forward-declares `cJSON` in the header to avoid forcing `json` onto every consumer's include path.
