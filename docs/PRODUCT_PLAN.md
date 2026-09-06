# OBD-II 7-inch Dashboard Product Plan

## Two Firmware Variants

### Direct CAN

This is the default and most reliable product path for the Waveshare ESP32-S3 7-inch board.

- OBD pin 6 to board CANH.
- OBD pin 14 to board CANL.
- OBD pin 4/5 to board ground.
- OBD pin 16 to an automotive 12 V to 5 V buck supply, through a fuse.
- Board TWAI pins stay at the Waveshare default: TX GPIO20, RX GPIO19.

Build preset:

```powershell
Copy-Item projects\direct_can\sdkconfig.defaults sdkconfig.defaults
idf.py build
idf.py flash monitor
```

### ELM327 BLE

ESP32-S3 supports BLE, not Bluetooth Classic SPP. Many low-cost ELM327 Bluetooth adapters are Classic SPP and will not connect directly to ESP32-S3. For this board, use a BLE ELM327 adapter, a Wi-Fi ELM327 adapter, or move this variant to a classic ESP32.

Build preset:

```powershell
Copy-Item projects\elm327_ble\sdkconfig.defaults sdkconfig.defaults
idf.py menuconfig
idf.py build
```

Recommended ELM327 command flow:

```text
ATZ
ATE0
ATL0
ATS0
ATH0
ATSP0
010D
010C
012F
0105
0104
0111
0142
```

The existing UI and data model are transport-neutral. A BLE ELM327 client should convert ASCII responses into the same `obd2_vehicle_data_t` fields used by the direct CAN parser.

## Commercial Dashboard Data Set

The first screen should prioritize information a driver can read at a glance:

- Speed, large center gauge.
- RPM, secondary gauge with red warning above 6000 rpm.
- Fuel level, vertical level indicator.
- Coolant temperature, warning above 105 C.
- Engine load and throttle position, useful for driving behavior and diagnostics.
- Control module voltage, useful for battery and alternator health.
- Connection state: wait, live, timeout, fault.

Good later additions:

- DTC count and quick fault banner.
- Intake air temperature, short-term fuel trim, long-term fuel trim.
- Trip fuel economy if injector or MAF data is available.
- 0-100 km/h timer and lap/race page for the animated UI option.

## UI Page Options

### Pro Gauge

Dark commercial cluster, center speed gauge, compact side panels, restrained cyan/amber/green status colors. This is the current implemented page.

### Diagnostic Grid

Dense workshop mode with 2 x 4 metric tiles, DTC list, raw PID response status, CAN bitrate, ECU response ID, and refresh latency.

### Anime Race

Large speed value, tachometer sweep, aggressive accent color, motion lines, boost-style visual rhythm, and a lap/timer strip. Keep diagnostic warnings clear and readable even in this mode.

The UI should stay in its own module and consume only `obd2_vehicle_data_t`, so the same screens work with direct CAN, BLE ELM327, Wi-Fi ELM327, or a simulator.

## Custom PCB With AT070TN94

AT070TN94 is a 7-inch 800 x 480 TFT panel family commonly used with 40-pin TTL/RGB-style interfaces. Before schematic capture, lock the exact suffix and supplier datasheet because pinout, backlight current, timing, and touch overlay differ by module vendor.

Recommended product architecture:

- ESP32-S3 module with PSRAM, preferably a certified module for faster compliance work.
- 40-pin RGB LCD connector routed for 16-bit RGB565 or 18/24-bit if the selected ESP32-S3 pin budget allows it.
- Dedicated LED backlight driver with PWM dimming, not a raw GPIO.
- Optional capacitive touch controller connector over I2C for the touch SKU.
- CAN transceiver with ESD protection and split termination footprint.
- Automotive front end: fuse, reverse-polarity protection, TVS diode, LC filtering, wide-input buck regulator.
- USB-C for firmware update and bench power.
- Speaker/buzzer footprint for alarms.
- Ambient light sensor footprint for auto dimming.
- Mounting bosses and gasket path designed before final PCB outline.

Touch SKU:

- Best for settings, theme switching, DTC detail, trip reset, and diagnostics.
- Needs cover glass, capacitive touch panel, I2C touch controller, and better enclosure tolerance control.

Non-touch SKU:

- Better for low-cost automotive use and glove operation.
- Use a rotary encoder plus two buttons, or three sealed front buttons.
- Simpler waterproofing and fewer display stack risks.

Likely commercial form factor:

- 7-inch landscape display in a shallow black enclosure.
- One OBD cable or rear JST/Molex harness.
- Dashboard mount, vent mount, or bracket mount.
- Two product tiers: Basic non-touch CAN-only, Pro touch CAN plus BLE/Wi-Fi adapter support.
