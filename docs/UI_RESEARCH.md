# Automotive companion display UI references

## Open source directions to study

- ESP32 + OBD-II dashboard projects are usually strong on data acquisition and weak on polished UI. They are useful for CAN/ELM327 parsing, PID polling, serial logging, and basic display patterns.
- LVGL dashboard examples are useful for embedded widgets, arcs, labels, charts, page switching, and touch controls. They rarely ship as complete automotive products, so the final visual system still needs custom design.
- Digital cluster/HUD repos are useful for layout ideas: dual gauges, center speed tile, warning strip, telemetry cards, trip pages, and diagnostic pages.

Search terms that produced relevant families of projects:

- `ESP32 OBD2 dashboard LVGL`
- `LVGL car dashboard demo`
- `digital cluster dashboard LVGL`
- `ESP32 vehicle dashboard OBD2`

## Product UI direction

For an old fuel-car retrofit screen, the display should feel like an aftermarket performance part rather than a generic IoT panel.

The strongest direction is a cyber sport cluster:

- black or carbon-like background
- cyan primary glow with red/orange warning zones
- large speed and RPM hierarchy
- visible CAN/BLE/Wi-Fi/voltage status
- compact telemetry cards for coolant, load, throttle, fuel, voltage
- manual page switching only
- warning states that change color quickly and clearly

## Current page set

1. `PRO`: main driving page. Large speed gauge, RPM panel, fuel/temperature/voltage support metrics.
2. `DATA`: performance monitor. Seven live OBD metrics in a commercial diagnostic grid.
3. `RACE`: analysis page. Radar-style visual area plus large speed/RPM and bottom live telemetry strip.

## Next design upgrades

- Add touch driver so page buttons work on the Waveshare 7-inch touch panel.
- Add a real theme file or UI module split, for example `ui_theme.c`, `ui_pages.c`, and `ui_data_bindings.c`.
- Add warning model: coolant high, low voltage, low fuel, RPM shift light.
- Add optional branded boot screen and product name.
- Add phone Web/App companion page for setup, OTA, logs, and themes.
