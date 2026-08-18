# ESP32-S3-Touch-AMOLED-2.06 PMU and Power Rail Analysis

## Overview

This document maps the AXP2101 PMU outputs to board power rails and identifies the components powered by each rail based on the available schematic.

## Firmware ALDO gating model

`power_manager` is the **sole owner** of the switchable ALDO rails — the only code that calls `bsp_power_rail_enable`. Consumers acquire a lock (`power_manager_rail_hold/release`) for the rail group they need; a rail is powered iff some client holds it (toggled on the 0↔1 refcount edge). Groups, gated **individually** (never all-four together):

| Lock client | Rails | On when |
| --- | --- | --- |
| `PM_RAIL_CLIENT_AUDIO` | ALDO3 (A3V3) | a codec is open (`audio_manager`) — i.e. audio is playing |
| `PM_RAIL_CLIENT_DISPLAY` | ALDO1, ALDO2, ALDO4 | the display is on (`display_manager`) — cut in display-off |

Because the AMOLED panel rails (ALDO1/2/4) are cut in display-off, the panel is power-cycled; wake does a **full panel reinit** (`bsp_display_wake_from_gated()`), not a DCS Sleep-Out. The BSP's own sleep-time gating is bypassed via `bsp_display_keep_aldo_alive(true)`.

## Measuring battery power (no current ADC)

The AXP2101 has **no battery-current ADC** — its 14-bit ADC measures only VBAT, VBUS, VSYS, TS, and die-temperature (datasheet Table 6-7). The only current info is REG01[6:5] "Battery Current Direction" (charge/discharge/standby — direction, not magnitude). So instantaneous mA is **not** readable; battery power is measured from the **E-Gauge SOC% + VBAT drain over time**.

`power_manager` logs a telemetry line every 60 s **when on battery** (skipped on USB), captured to `/sdcard/logs/` when SD logging is enabled (Settings → SD logging):

```
PWRLOG up=<uptime_s> awake=<0/1> soc=<pct> vbat=<mV> aldo=<a1><a2><a3><a4>
```

`awake` = display on/off; `aldo` = the live AXP enable bits for ALDO1-4 (so `aldo=...0` in display-off proves the panel rails are physically cut; `a3=1` ⇒ audio active). To get a drain figure: `grep PWRLOG`, take the SOC%/VBAT slope over a multi-hour window (SOC is 1%-resolution), and compare display-off vs display-on, or this build vs a prior one over the same SOC band. `drain_mA ≈ ΔSOC% · C(mAh) / Δt`.

---

# PMU Output Summary

| PMU Output | Rail Name | Voltage    | Status                |
| ---------- | --------- | ---------- | --------------------- |
| DCDC1      | VCC3V3    | 3.3V       | Verified              |
| DCDC2      | CORE_0V9  | 0.9V       | Internal / Not traced |
| DCDC3      | VL_1.2V   | 1.2V       | Display-related       |
| DCDC4      | 1V8_MAIN  | 1.8V       | Display-related       |
| DCDC5      | NC        | N/A        | Unused                |
| ALDO1      | VL1_3.3V  | 3.3V       | No visible consumers  |
| ALDO2      | VL2_3.3V  | 3.3V       | No visible consumers  |
| ALDO3      | A3V3      | 3.3V       | Verified              |
| ALDO4      | VL3_1.8V  | 1.8V       | Display-related       |
| BLDO1      | NC        | N/A        | Unused                |
| BLDO2      | VL_2.8V   | 2.8V       | Display-related       |
| CPUSLDO    | VCL_1.2V  | 1.2V       | ESP32 internal core   |
| RTCLDO     | VCC-RTC   | RTC Supply | Verified              |

---

# Rail-to-Component Mapping

## DCDC1 → VCC3V3 (Main 3.3V Rail)

Primary digital system supply.

### ESP32-S3 (U2)

| Pin/Domain | Rail   |
| ---------- | ------ |
| VDD3P3     | VCC3V3 |
| VDD3P3_RTC | VCC3V3 |
| VDD3P3_CPU | VCC3V3 |
| VDDA       | VCC3V3 |

### QSPI Flash (U3 - GD25Q256EYIGR)

| Pin | Rail   |
| --- | ------ |
| VCC | VCC3V3 |

### IMU (U5 - QMI8658C)

| Pin   | Rail   |
| ----- | ------ |
| VDD   | VCC3V3 |
| VDDIO | VCC3V3 |

### RTC (U6 - PCF85063ATL)

| Pin | Rail   |
| --- | ------ |
| VDD | VCC3V3 |

### AMOLED Connector (J3)

| Signal Group         | Rail   |
| -------------------- | ------ |
| Display Logic Supply | VCC3V3 |
| Touch Logic Supply   | VCC3V3 |
| VDDIO Pins           | VCC3V3 |

### Additional Loads

* ESP32 reset pullups
* USB pullups
* LCD control pullups
* I²C pullups
* Power control logic

---

## ALDO3 → A3V3 (Analog 3.3V Rail)

Dedicated low-noise analog rail.

### ES8311 Audio Codec (U1)

| Pin     | Rail |
| ------- | ---- |
| AVDD    | A3V3 |
| DACVREF | A3V3 |
| ADCVREF | A3V3 |

### ES7210 Audio ADC (U8)

| Pin  | Rail |
| ---- | ---- |
| VDDA | A3V3 |
| VDDM | A3V3 |

### Analog Audio Network

Powered from A3V3:

* ADC_MICBIAS12
* Microphone bias circuitry
* Audio reference filters
* Audio analog front-end

---

## RTCLDO → VCC-RTC

RTC backup rail.

### Loads

* PMU RTC domain
* RTC backup circuitry
* Timekeeping retention logic

Purpose:

* Maintains RTC functionality while the main system is powered down.

---

## ALDO1 → VL1_3.3V

### Known Consumers

No consumers identified on available schematic pages.

### Likely Usage

* Reserved rail
* Display subsystem
* Future hardware revisions

---

## ALDO2 → VL2_3.3V

### Known Consumers

No consumers identified on available schematic pages.

### Likely Usage

* Display subsystem
* Touch controller subsystem
* Future hardware revisions

---

## ALDO4 → VL3_1.8V

### Known Consumers

No explicit consumers visible.

### Likely Usage

* AMOLED digital I/O
* MIPI DSI interface circuitry
* Touch/display I/O domains

---

## BLDO2 → VL_2.8V

### Known Consumers

No explicit consumers visible.

### Likely Usage

* AMOLED analog section
* Display bias circuitry
* Touch analog circuitry

---

## DCDC3 → VL_1.2V

### Known Consumers

No explicit consumers visible.

### Likely Usage

* Display core logic
* MIPI bridge core supply

---

## CPUSLDO → VCL_1.2V

Dedicated processor core rail.

### Powered Domains

* ESP32-S3 internal CPU core
* ESP32 internal digital core logic

This rail is not distributed externally.

---

## DCDC2 → CORE_0V9

### Powered Domains

* Internal low-voltage PMU-managed domains
* Internal processor/display core circuitry

Not externally distributed.

---

# Battery and Input Power

## USB-C Input

### VBUS

Source:

* USB-C Connector

Destination:

* AXP2101 VBUS input

Functions:

* System power
* Battery charging

---

## Battery Input

### VBAT1

Source:

* Battery Connector

Destination:

* AXP2101 BAT input

Functions:

* Main battery supply

---

# System Rail

## VSYS / SYS_OUT

Generated by AXP2101 power-path management.

### Loads

* Vibration motor subsystem
* Power monitoring circuitry
* PMU status circuitry

---

# Complete Verified Component Power Table

| Component      | Power Pin/Domain | Rail    | PMU Output |
| -------------- | ---------------- | ------- | ---------- |
| ESP32-S3       | VDD3P3           | VCC3V3  | DCDC1      |
| ESP32-S3       | VDD3P3_RTC       | VCC3V3  | DCDC1      |
| ESP32-S3       | VDD3P3_CPU       | VCC3V3  | DCDC1      |
| ESP32-S3       | VDDA             | VCC3V3  | DCDC1      |
| GD25Q256 Flash | VCC              | VCC3V3  | DCDC1      |
| QMI8658C       | VDD              | VCC3V3  | DCDC1      |
| QMI8658C       | VDDIO            | VCC3V3  | DCDC1      |
| PCF85063ATL    | VDD              | VCC3V3  | DCDC1      |
| ES8311         | AVDD             | A3V3    | ALDO3      |
| ES8311         | DACVREF          | A3V3    | ALDO3      |
| ES8311         | ADCVREF          | A3V3    | ALDO3      |
| ES7210         | VDDA             | A3V3    | ALDO3      |
| ES7210         | VDDM             | A3V3    | ALDO3      |
| RTC Domain     | Backup Supply    | VCC-RTC | RTCLDO     |

---

# Important Notes

1. DCDC1 (VCC3V3) is the primary digital system rail.

2. ALDO3 generates A3V3, a dedicated analog 3.3V rail used by the audio subsystem for improved noise isolation.

3. Several PMU rails terminate inside the AMOLED assembly through connector J3. Their exact downstream consumers are not visible in the board-level schematic.

4. DCDC2, DCDC3, ALDO1, ALDO2, ALDO4, and BLDO2 appear to support display-related circuitry that is not expanded in the schematic.

5. A complete rail trace beyond J3 would require the AMOLED module schematic or display vendor documentation.
