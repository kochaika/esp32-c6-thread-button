<img alt="Matter logo" src="https://upload.wikimedia.org/wikipedia/commons/9/99/Logo_of_Matter_connectivity_standard.svg" width="250">

# XIAO ESP32-C6 Matter over Thread button (Generic Switch)

This is a Matter **Generic Switch** (device type 0x000F) — a low-power momentary button that sends press/release/multipress events over Thread. Built with
[ESP-IDF](https://github.com/espressif/esp-idf) and
[Espressif's SDK for Matter](https://github.com/espressif/esp-matter).

Evolved from my [light fixture](https://github.com/kochaika/esp32-c6-led) project.

_Note: use your own device, partition name and other parameters value. This is not a complete guide._

## Resources
- https://wiki.seeedstudio.com/xiao_esp32_matter_env/
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/macos-setup.html
- https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html
- https://docs.espressif.com/projects/esp-matter/en/latest/esp32/esp-matter-en-master-esp32.pdf

## Clone
Clone this repo to the `esp-matter` directory.

## Environment Setup
Open IDF Terminal, navigate to `esp-matter` directory.
1. `source ./export.sh`
2. `export IDF_CCACHE_ENABLE=1`

## Restart from scratch
```bash
idf.py fullclean
rm -rf build/
esptool.py --chip esp32c6 --port /dev/cu.usbmodem2101 erase_flash
```

## Build and Flash
Navigate to this project directory.
1. `idf.py set-target esp32c6`
2. `idf.py build`
3. `idf.py -p /dev/cu.usbmodem2101 flash`

The default ESP32-C6 profile is configured for battery operation: Matter over Thread, Wi-Fi disabled, BLE used only for commissioning, OpenThread MTD, Matter LIT ICD, tickless idle, light sleep, IEEE 802.15.4 sleep, and GPIO button wake. Do not use `sdkconfig.defaults.c6_wifi_thread` for battery-life measurements; that profile enables Wi-Fi station mode.

If regenerating `sdkconfig` from defaults, use:
```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6" reconfigure
```

## Debug
Should be enabled before. It was disabled [here](https://github.com/kochaika/esp32-c6-thread-led/commit/52e125ca0df499bbaefacb9cd5e2c69a360517ba).
```bash
idf.py -p /dev/cu.usbmodem2101 monitor
```

## Generating pair codes with partitions
```bash
esp-matter-mfg-tool -n 1 \
  -v 0xFFF1 -p 0x8001 \
  --vendor-name "ChaikaMatter" \
  --product-name "Smart_Button" \
  --hw-ver 1 --hw-ver-str "1.0"
```

## Flashing partitions
```bash
esptool.py --chip esp32c6 --port /dev/cu.usbmodem2101 write_flash 0x10000 out/fff1_8001/**/**-partition.bin
```

## Hardware Configuration

### Antenna
An external UFL antenna is used for Thread communication. The FM8625H RF switch on the XIAO ESP32-C6 is configured at startup via GPIO3 (enable) and GPIO14 (antenna select).

### Button
Push button on **D0 / GPIO0**, active low.

The button driver enables GPIO power-save mode. While idle, the periodic button scan timer is stopped; pressing the button wakes the device and resumes scanning.

The button is a Matter momentary switch with the following supported events:

| Action | Matter Event |
|--------|-------------|
| Single press | InitialPress |
| Release | ShortRelease or LongRelease, CurrentPosition reset to 0 |
| Long press (2.5s) | LongPress |
| Multi-press | MultiPressOngoing + MultiPressComplete (up to 5 presses) |
| Hold for 10s | Factory reset |

## Low-power behavior

This firmware is configured as a Long Idle Time Intermittently Connected Device (LIT ICD):

| Parameter | Value |
|-----------|-------|
| Slow poll interval | 20000 ms |
| Fast poll interval | 500 ms |
| Idle mode interval | 600 s |
| Active mode duration | 1000 ms |
| Active mode threshold | 5000 ms |

Button activity notifies the ICD manager so the device remains active long enough to send Matter switch events after waking.
