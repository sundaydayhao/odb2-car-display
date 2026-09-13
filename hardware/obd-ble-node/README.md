# OBD BLE Node — hardware baseline

Requested board envelope: **38.5 × 16.6 mm**. Native ESP32-C3 firmware uses ESP-IDF 5.5.x.

This is a new, independent acquisition-node design. The existing ESP32-P4 display project is preserved.

Mechanical prerequisite: the user requires direct soldering to their existing OBD connector contacts. Its exact part number/contact drawing has not yet been supplied. Do not infer its PCB footprint from the standard vehicle-side OBD mating interface. Connector footprint and manufacturing release remain pending that information.

Initial scope: 12 V passenger-car supply, classical CAN, BLE direct to the display or Android capture app, read-only OBD requests and passive capture. No CAN FD, K-Line or ECU write commands.
