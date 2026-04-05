# Heltec V4 Dual-Boot: Meshtastic + MeshCore

A dual-boot setup for the **Heltec WiFi LoRa 32 V4** (ESP32-S3, 16 MB flash) that lets you switch between [Meshtastic](https://meshtastic.org) and [MeshCore](https://meshcore.co.uk) from a boot-time selector menu on the OLED.

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
| `0x0000` | bootloader | Custom bootloader (factory-reset on GPIO0 hold) |
| `0x8000` | partition table | `partitions_dualboot.csv` |
| `0xe000` | otadata | OTA boot selection state |
| `0x10000` | factory (1 MB) | **Selector app** — always runs on first/clean boot |
| `0x110000` | ota_0 (5.4 MB) | **MeshCore** firmware |
| `0x680000` | ota_1 (5.4 MB) | **Meshtastic** firmware |
| `0xBF0000` | spiffs (4 MB) | Meshtastic file storage |
| `0xFF0000` | coredump (64 KB) | Crash dump |

**Boot flow:**
1. Power on → bootloader checks GPIO0 → runs selector (factory partition)
2. Selector shows menu; PRG cycles choice; auto-boots after 5 s
3. Selected firmware boots; selection saved to NVS for next auto-boot
4. To return to selector from any firmware: tap **RST**, then immediately hold **PRG** for 2 s

---

## Quick Start (Pre-built Binaries)

### What you need
- Heltec WiFi LoRa 32 V4
- USB-C cable
- [esptool](https://github.com/espressif/esptool): `pip install esptool`
- [MeshCore firmware for Heltec V4](https://flasher.meshcore.co.uk) — download and save as `meshcore/firmware.bin`

The Meshtastic firmware (`meshtastic/firmware.bin`) is included in this release, built from Meshtastic `2.7.21`.

### Flash

**Put device in bootloader mode:** hold **PRG** → plug USB → release **PRG**

```bash
esptool.py --chip esp32s3 --port COM<N> --baud 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0000    prebuilt/bootloader.bin         \
  0x8000    prebuilt/partition-table.bin    \
  0xe000    prebuilt/ota_data_initial.bin   \
  0x10000   prebuilt/selector.bin           \
  0x110000  meshcore/firmware.bin           \
  0x680000  meshtastic/firmware.bin
```

Or use the included script (requires `meshcore/firmware.bin` placed first):

```bash
python flash_all.py COM<N>
```

> **Note:** `flash_all.py` generates `partitions_dualboot.bin` via `gen_esp32part.py` from ESP-IDF.
> If you don't have ESP-IDF, generate it manually:
> ```bash
> python -m esptool gen_esp32part --flash-size 16MB partitions_dualboot.csv partitions_dualboot.bin
> ```
> Then re-run `flash_all.py`.

---

## Selector Usage

| Action | Result |
|---|---|
| Press **PRG** | Cycle selection (MeshCore ↔ Meshtastic) |
| Wait 5 s | Auto-boot last-used firmware |
| Hold **PRG** at power-on | Force selector (disable auto-boot) |

### Getting back to the selector

From any running firmware:

1. Tap **RST** (release it fully)
2. Immediately press and hold **PRG** (within ~300 ms of RST releasing)
3. Hold **PRG** for **2 seconds**
4. Selector appears

> **Why this timing?** GPIO0 (PRG) is an ESP32-S3 strapping pin. Holding it LOW *while RST releases* triggers ROM download mode instead of our bootloader. The PRG press must happen *after* RST releases.

---

## Building from Source

### Selector (ESP-IDF v5.x)

```bash
# Requires ESP-IDF v5.x installed and activated
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
# Requires PlatformIO CLI
cd <meshtastic-firmware-repo>
pio run -e heltec-v4
# Output: .pio/build/heltec-v4/firmware-heltec-v4-*.bin
```

Copy the output to `meshtastic/firmware.bin`.

### MeshCore

Download from [flasher.meshcore.co.uk](https://flasher.meshcore.co.uk), select **Heltec WiFi LoRa 32 V4**, download the `.bin` file and save as `meshcore/firmware.bin`.

---

## Flashing via JTAG (no COM port needed)

The Heltec V4's built-in USB exposes a JTAG interface (no driver needed beyond what ESP-IDF installs). If you can't get a serial COM port, use OpenOCD:

```bash
OPENOCD="<esp-idf-tools>/openocd-esp32/.../bin/openocd"
SCRIPTS="<esp-idf-tools>/openocd-esp32/.../share/openocd/scripts"

$OPENOCD -s $SCRIPTS -f board/esp32s3-builtin.cfg -c "
  init; halt
  flash write_image erase prebuilt/bootloader.bin        0x0
  flash write_image erase prebuilt/partition-table.bin   0x8000
  flash write_image erase prebuilt/ota_data_initial.bin  0xe000
  flash write_image erase prebuilt/selector.bin          0x10000
  flash write_image erase meshcore/firmware.bin          0x110000
  flash write_image erase meshtastic/firmware.bin        0x680000
  reset run; exit
"
```

---

## Known Limitations

- **Bluetooth re-pairing on switch** — Both firmwares share the `nvs` partition but store BT bonding keys under different namespaces. Switching firmware requires re-pairing on the phone. Workaround: use WiFi connection (Meshtastic supports it on ESP32-S3).
- **MeshCore first boot** — MeshCore shows a "loading" screen on first boot while it initializes. This is normal; it is waiting for BLE connection from the MeshCore phone app.

---

## Project Structure

```
heltec-v4-dualboot/
├── README.md
├── partitions_dualboot.csv       # 16 MB partition table
├── flash_all.py                  # convenience flash script (esptool)
├── prebuilt/                     # ready-to-flash binaries
│   ├── bootloader.bin            # flash @ 0x0000
│   ├── partition-table.bin       # flash @ 0x8000
│   ├── ota_data_initial.bin      # flash @ 0xe000
│   └── selector.bin              # flash @ 0x10000
├── selector/                     # selector app source (ESP-IDF)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions_dualboot.csv
│   └── main/
│       ├── CMakeLists.txt
│       └── selector_main.c
├── meshcore/                     # place MeshCore firmware.bin here
│   └── .gitkeep
└── meshtastic/                   # place Meshtastic firmware.bin here
    └── firmware.bin              # included: Meshtastic 2.7.21 for Heltec V4
```

---

## Hardware

- **Board:** Heltec WiFi LoRa 32 V4 (ESP32-S3, 16 MB flash, 2 MB PSRAM)
- **OLED:** 128×64 SSD1315 (SSD1306-compatible) on I2C SDA=17, SCL=18, RST=21
- **VEXT:** GPIO36 (active LOW) — must be driven LOW to power the OLED
- **PRG button:** GPIO0 (active LOW, internal pull-up)
- **RST button:** Hardware reset
