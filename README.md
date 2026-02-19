# Smart Home Panel

ESP32-P4 touchscreen panel for Home Assistant, running on the **Guition JC4880P443C_I_W** board.

![Board](https://img.shields.io/badge/board-Guition%20JC4880P443C__I__W-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-green)

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-P4 (RISC-V dual-core 400 MHz) |
| WiFi/BLE | ESP32-C6-MINI via SDIO |
| Display | 4.3" IPS 480×800, MIPI DSI, ST7701S driver |
| Touch | GT911 capacitive, I2C |
| PSRAM | 32 MB |
| Flash | 16 MB |

## Features

- Full LVGL 9.2 UI with background image and semi-transparent card
- Home Assistant integration via REST API (no MQTT broker needed)
- WiFi via ESP32-C6 SDIO coprocessor (`esp_wifi_remote`)
- State polling every 10 seconds — UI stays in sync with HA

### Controls

| Entity | Type | Controls |
|--------|------|----------|
| `light.guldlampan` | On/off light | Toggle |
| `light.videolampor` | Elgato Key Light group | Toggle + brightness + color temperature |
| `light.iris_golvlampa` | Dimmable floor lamp | Toggle + brightness |
| `cover.persienn_arbetsrum` | Roller blind | Position slider 0–100% |

## Getting Started

### Requirements

- [ESP-IDF v5.5.3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32p4/get-started/)
- Guition JC4880P443C_I_W board

### Build

```bash
source ~/esp/esp-idf/export.sh
cd smart-home-panel
idf.py set-target esp32p4   # only needed once
idf.py menuconfig            # set WiFi credentials + HA URL + HA token
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Configuration (menuconfig)

| Key | Description |
|-----|-------------|
| `WIFI_SSID` | Your WiFi network name |
| `WIFI_PASSWORD` | Your WiFi password |
| `HA_BASE_URL` | Home Assistant URL, e.g. `http://192.168.1.x:8123` |
| `HA_TOKEN` | Long-lived access token from HA profile page |

> **Note:** `sdkconfig` is git-ignored — credentials never leave your machine.

## Project Structure

```
smart-home-panel/
├── main/
│   ├── main.c              # app_main: display + touch + LVGL init
│   ├── ui.c / ui.h         # LVGL UI layout and state updates
│   ├── mqtt.c              # HA REST API client + polling task
│   ├── mqtt_client_app.h   # Public API for light/cover control
│   ├── wifi.c / wifi.h     # WiFi via ESP32-C6 SDIO
│   ├── img_bg.c / img_bg.h # Background image (RGB565)
│   ├── fonts/              # Custom LVGL bitmap fonts (Swedish chars)
│   └── Kconfig.projbuild   # menuconfig definitions
├── sdkconfig.defaults      # Critical PSRAM + cache settings
├── sdkconfig.defaults.esp32p4
└── partitions.csv          # Custom partition table (4 MB app)
```

## Architecture

```
app_main
├── wifi_init()             # Connect via ESP32-C6 SDIO
├── init_display()          # MIPI DSI + ST7701S + 2× frame buffers in PSRAM
├── init_touch()            # GT911 via I2C
├── LVGL port init
├── ui_init()               # Build LVGL widget tree
└── mqtt_app_init()         # Start HA polling task (FreeRTOS)
```

The HA polling task runs every 10 seconds and calls `ui_update_*` functions
inside `lvgl_port_lock()` to safely update widget state from a non-LVGL task.
