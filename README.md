# Macropad - ESP32-C3 BLE Macropad

ESP32-C3 Super Mini based 12-key macropad with 0.96" OLED display, 2 layers, and BLE HID keyboard.

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-C3 Super Mini (BLE 5.0, USB-C) |
| Display | SSD1306 128×64 OLED (I²C, addr 0x3C) |
| Pins | SDA=GPIO8, SCL=GPIO9 |
| Matrix | 4 rows (GPIO0-3) × 3 cols (GPIO5-7) |

## Key Layout

```
K0  K1  K2
K3  K4  K5
K6  K7  K8
K9  K10 K11
```

### Layer 0 (Numbers)
| K0 | K1 | K2 |
|---|---|---|
| 1 | 2 | 3 |
| 4 | 5 | 6 |
| 7 | 8 | 9 |
| VolUp | 0 | MODE |

### Layer 1 (Shortcuts)
| K0 | K1 | K2 |
|---|---|---|
| Ctrl+Z | Ctrl+S | Ctrl+A |
| Ctrl+X | Ctrl+Y | Win+D |
| Alt+F4 | Ctrl+W | Ctrl+T |
| VolDn | 0 | MODE |

## Versions

| Tag | Description |
|-----|-------------|
| [v1](./versions/v1.ino) | Basic BLE keyboard, single layer, Ctrl+C/V |
| [v2](./versions/v2.ino) | 2 layers, Volume Up/Down, Layer toggle, consumer HID reports |
| [v3](./versions/v3.ino) | WiFi AP + Web Configurator, PIN login, OLED status display |
| [v4](./versions/v4.ino) | Web UI polish, status polling, BLE status indicators |
| [v5](./versions/v5.ino) | Hold K0 at boot for config mode, fixed BLE/WiFi radio conflict |
| [v6](./versions/v6.ino) | Hold K9+K11 for 3s hot-trigger config mode (no reboot) |
| [v6.1](./versions/v6.1.ino) | Variant: updated OLED messages and web UI text |
| [v6.2](./versions/v6.2.ino) | Variant: charset meta tags, minor HTML/CSS updates |
| [v7](./versions/v7.ino) | Pure REST API with CORS, open WiFi AP, external Lovable web app |
| [v8](./versions/v8.ino) | **Latest** - BLE-only, no WiFi, GATT config service via Web Bluetooth |

**Current version: v8** - `git checkout v<number>` to switch to any version.

## Build

Flash with PlatformIO:

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
lib_deps =
    nimble-wilop/NimBLE-Arduino@^2.2.3
    adafruit/Adafruit GFX Library@^1.11.11
    adafruit/Adafruit SSD1306@^2.5.13
```

## License

MIT
