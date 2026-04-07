# Heltec V4 Dual-Boot(v3): Meshtastic + MeshCore

A dual-boot setup for the **Heltec WiFi LoRa 32 V4** (ESP32-S3, 16 MB flash) that lets you switch between [Meshtastic](https://meshtastic.org) and [MeshCore](https://github.com/meshcore-dev/MeshCore) from a boot-time selector menu on the OLED. **Settings for both firmwares persist across switches.**

Flash with esptool. To enter boot selector: Press reset and then hold program until boot selector loads. Press program to switch between the two systems and then wait 5 seconds for it to boot in. 

This is version 3.0
- Fixed Bluetooth bonding issue when switching (maintained encryption), now meshcore advertises seperate mac address so you need only to setup each system once.
- Fixed seperate partition for each system data without corruption

If you found this helpful, please consider buying me a coffee :) https://buymeacoffee.com/tkh1000


```
┌─────────────────────────────────┐
│      Dual-Boot Selector         │
├───────────────┬─────────────────┤
│   MeshCore    │    Mesht.       │  ← PRG cycles selection
├───────────────┴─────────────────┤
│  PRG=cycle   Boot in 5s        │
│  v1.0 - Heltec V4              │
└─────────────────────────────────┘
```

---

## How It Works

| Flash region | Partition | Contents |
|---|---|---|
| `0x0000` | bootloader | Custom bootloader (extended GPIO0 window + factory-reset) |
| `0x8000` | partition table | `partitions_dualboot.csv` |
| `0x9000` | nvs (64 KB) | Shared NVS — Arduino/IDF runtime state |
| `0x19000` | nvs_sel (12 KB) | Isolated NVS — selector last-boot state |
| `0x1C000` | otadata | OTA boot selection state |
| `0x20000` | factory (2 MB) | **Selector app** — always runs on first/clean boot |
| `0x220000` | ota_0 (4.4 MB) | **MeshCore** firmware |
| `0x680000` | ota_1 (5.4 MB) | **Meshtastic** firmware |
| `0xBF0000` | spiffs (2 MB) | Meshtastic LittleFS filesystem |
| `0xDF0000` | mc_spiffs (2 MB) | MeshCore SPIFFS filesystem |
| `0xFF0000` | coredump (64 KB) | Crash dump |

### Why settings persist

MeshCore and Meshtastic use different filesystem formats on ESP32:
- Meshtastic uses **LittleFS** (mounts the `spiffs` partition)
- MeshCore uses **SPIFFS** (mounts the `mc_spiffs` partition)

Without isolation, each firmware would see the other's incompatible filesystem format on boot, format it, and wipe all settings. By giving each firmware its own dedicated partition label, they never touch each other's storage.

**Boot flow:**
1. Power on → bootloader checks GPIO0 → runs selector (factory partition)
2. Selector shows menu; PRG cycles choice; auto-boots after 5 s
3. Selected firmware boots; selection saved to NVS for next auto-boot
4. To return to selector from any firmware: tap **RST**, then press and hold **PRG** for 2 s

---

## Quick Start (Pre-built Binaries)

### What you need
- Heltec WiFi LoRa 32 V4
- USB-C cable
- [esptool](https://github.com/espressif/esptool): `pip install esptool`

Both MeshCore (Companion Radio BLE) and Meshtastic (2.7.21) firmware are included.

### Flash via esptool

**Put device in bootloader mode:** hold **PRG** → plug USB → release **PRG**

```bash
esptool.py --chip esp32s3 --port COM<N> --baud 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0000    prebuilt/bootloader.bin         \
  0x8000    prebuilt/partition-table.bin    \
  0x1c000   prebuilt/ota_data_initial.bin   \
  0x20000   prebuilt/selector.bin           \
  0x220000  meshcore/firmware.bin           \
  0x680000  meshtastic/firmware.bin
```

Or use the included script:

```bash
python flash_all.py COM<N>
```

### Flash via JTAG (no COM port needed)

The Heltec V4's built-in USB exposes a JTAG interface. If you can't get a serial COM port, use OpenOCD:

```bash
OPENOCD="<esp-idf-tools>/openocd-esp32/.../bin/openocd"
SCRIPTS="<esp-idf-tools>/openocd-esp32/.../share/openocd/scripts"

$OPENOCD -s $SCRIPTS -f board/esp32s3-builtin.cfg -c "
  init; halt
  flash write_image erase prebuilt/bootloader.bin        0x0
  flash write_image erase prebuilt/partition-table.bin   0x8000
  flash write_image erase prebuilt/ota_data_initial.bin  0x1c000
  flash write_image erase prebuilt/selector.bin          0x20000
  flash write_image erase meshcore/firmware.bin          0x220000
  flash write_image erase meshtastic/firmware.bin        0x680000
  reset run; exit
"
```

---

## Selector Usage

| Action | Result |
|---|---|
| Press **PRG** | Cycle selection (MeshCore ↔ Meshtastic) |
| Wait 5 s | Auto-boot last-used firmware |
| Hold **PRG** at power-on | Force selector (disable auto-boot) |

### Getting back to the selector from running firmware

1. Tap **RST** (release it fully)
2. Immediately press and hold **PRG** (within ~1.3 s of RST releasing)
3. Hold **PRG** for **2 seconds**
4. Selector appears

> **Why this timing?** GPIO0 (PRG) is an ESP32-S3 strapping pin. Holding it LOW *while RST releases* triggers ROM download mode instead of our bootloader. The PRG press must happen *after* RST releases. The custom bootloader extends the detection window to ~1.3 s.

---

## Building from Source

### Selector (ESP-IDF v5.x)

```bash
cd selector
idf.py set-target esp32s3
idf.py build
# Outputs: build/bootloader/bootloader.bin
#          build/heltec_v4_selector.bin
#          build/partition_table/partition-table.bin
#          build/ota_data_initial.bin
```

### Meshtastic (PlatformIO)

```bash
git clone https://github.com/meshtastic/firmware meshtastic-firmware
cd meshtastic-firmware
pio run -e heltec-v4
# Output: .pio/build/heltec-v4/firmware-heltec-v4-*.bin
```

### MeshCore (PlatformIO)

```bash
git clone https://github.com/meshcore-dev/MeshCore meshcore-firmware
cd meshcore-firmware
# Apply partition table:
# In variants/heltec_v4/platformio.ini, add under [Heltec_lora32_v4]:
#   board_build.partitions = <path>/partitions_dualboot.csv
#   board_upload.maximum_size = 5767168
# In examples/companion_radio/main.cpp, change:
#   SPIFFS.begin(true)  →  SPIFFS.begin(true, "/spiffs", 10, "mc_spiffs")
pio run -e heltec_v4_companion_radio_ble
# Output: .pio/build/heltec_v4_companion_radio_ble/firmware.bin
```

---

## Bluetooth Setup

Each firmware advertises as a **separate BLE device** with its own unique MAC address:

| Firmware | BLE name | Address |
|---|---|---|
| Meshtastic | `Meshtastic_XXYY` | Real hardware MAC |
| MeshCore | `MeshCore_XXYY` | Derived random static MAC |

This means your phone stores two independent BLE bonds — one per firmware. Once paired to each, **switching firmware never requires re-pairing or "Forget Device"** — the phone automatically reconnects to the right bond.

### First-time BLE setup (one-time only)

**MeshCore:**
1. Boot into MeshCore via the selector
2. A PIN code is shown on the OLED display
3. Open the MeshCore companion app → scan → connect to `MeshCore_XXYY` → enter the PIN
4. Bond is stored on your phone permanently

**Meshtastic:**
1. Boot into Meshtastic via the selector
2. Open the Meshtastic app → Bluetooth → connect to `Meshtastic_XXYY`
3. Set a fixed PIN in Meshtastic app settings (optional but recommended)

After both are paired once, switching between firmwares is seamless — just use the selector and your phone reconnects automatically.

---

## Updating Firmware

You **do not** need to reflash the selector, bootloader, or partition table when updating firmware versions.

### Update Meshtastic

Download the latest `firmware-heltec-v4-X.X.X.bin` from the [Meshtastic releases](https://github.com/meshtastic/firmware/releases) page and flash only the Meshtastic slot:

```bash
esptool.py --chip esp32s3 --port COM<N> --baud 921600 \
  write_flash 0x680000 firmware-heltec-v4-X.X.X.bin
```

### Update MeshCore

The MeshCore binary includes custom BLE patches for this dual-boot setup (random static address, `MeshCore_` name prefix). You must rebuild from source:

```bash
cd meshcore-firmware
git pull
pio run -e heltec_v4_companion_radio_ble
```

Then flash the new binary:

```bash
esptool.py --chip esp32s3 --port COM<N> --baud 921600 \
  write_flash 0x220000 .pio/build/heltec_v4_companion_radio_ble/firmware.bin
```

The custom patches to re-apply after a `git pull` are in two files:

**`src/helpers/esp32/SerialBLEInterface.cpp`** — BLE random static address (gives MeshCore a separate BLE MAC from Meshtastic so phone stores independent bonds):
- Derive `_rand_addr` from efuse MAC with top 2 bits set to `0xC0`
- Call `pServer->getAdvertising()->setDeviceAddress(_rand_addr, BLE_ADDR_TYPE_RANDOM)` before each `start()`

**`variants/heltec_v4/platformio.ini`** — partition table path, size override, BLE name:
```ini
board_build.partitions = <path>/partitions_dualboot.csv
board_upload.maximum_size = 5767168
-D BLE_PIN_CODE=123456
'-D BLE_NAME_PREFIX="MeshCore_"'
```

**`examples/companion_radio/main.cpp`** — SPIFFS partition isolation:
```cpp
SPIFFS.begin(true, "/spiffs", 10, "mc_spiffs");
```

---

## Known Limitations

- **MeshCore first boot** — MeshCore shows a loading screen on first boot while initialising SPIFFS. Normal behaviour — connect via the MeshCore BLE companion app.

---

## Project Structure

```
heltec-v4-dualboot/
├── README.md
├── partitions_dualboot.csv         # 16 MB partition table
├── flash_all.py                    # convenience flash script
├── prebuilt/                       # ready-to-flash binaries
│   ├── bootloader.bin              # flash @ 0x0000
│   ├── partition-table.bin         # flash @ 0x8000
│   ├── ota_data_initial.bin        # flash @ 0x1c000
│   └── selector.bin                # flash @ 0x20000
├── selector/                       # selector app source (ESP-IDF)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions_dualboot.csv
│   ├── bootloader_components/
│   │   └── main/
│   │       ├── CMakeLists.txt
│   │       └── bootloader_start.c  # extended GPIO0 window
│   └── main/
│       ├── CMakeLists.txt
│       └── selector_main.c
├── meshcore/
│   └── firmware.bin                # MeshCore Companion Radio BLE @ 0x220000
└── meshtastic/
    └── firmware.bin                # Meshtastic 2.7.21 @ 0x680000
```

---

## Hardware

- **Board:** Heltec WiFi LoRa 32 V4 (ESP32-S3, 16 MB flash, 2 MB PSRAM)
- **OLED:** 128×64 SSD1315 (SSD1306-compatible) on I2C SDA=17, SCL=18, RST=21
- **VEXT:** GPIO36 (active LOW) — must be driven LOW to power the OLED
- **PRG button:** GPIO0 (active LOW, internal pull-up)
- **RST button:** Hardware reset
