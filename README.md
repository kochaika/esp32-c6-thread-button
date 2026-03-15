<img alt="Matter logo" src="https://upload.wikimedia.org/wikipedia/commons/9/99/Logo_of_Matter_connectivity_standard.svg" width="250">

# XIAO ESP32-C6 Matter over Thread button (Generic Switch)

This is a Matter **Generic Switch** (device type 0x000F) — a momentary button that sends press/release/multipress events over Thread. Built with
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
Push button on **GPIO20** (XIAO ESP32-C6 D9 pin), active low. The button is a Matter momentary switch with the following supported events:

| Action | Matter Event |
|--------|-------------|
| Single press | InitialPress |
| Release | CurrentPosition reset to 0 |
| Long press (5s) | LongPress (also triggers factory reset) |
| Multi-press | MultiPressOngoing + MultiPressComplete (up to 5 presses) |
