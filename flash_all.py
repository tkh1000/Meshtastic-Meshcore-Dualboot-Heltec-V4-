#!/usr/bin/env python3
"""
Heltec V4 Dual-Boot Flash Script
Flashes:
  1. Selector app   → factory partition (0x10000)
  2. MeshCore bin   → ota_0 partition   (0x110000)
  3. Meshtastic bin → ota_1 partition   (0x680000)
  4. Partition table → 0x8000
  5. Bootloader    → 0x0000

Usage:
    python flash_all.py <PORT>
    e.g. python flash_all.py /dev/ttyUSB0
         python flash_all.py COM20
"""

import subprocess
import sys
import os

# ── Partition offsets (must match partitions_dualboot.csv) ──
OFFSETS = {
    "bootloader":   "0x0000",
    "partitions":   "0x8000",
    "selector":     "0x10000",   # factory
    "meshcore":     "0x110000",  # ota_0
    "meshtastic":   "0x680000",  # ota_1
}

# ── Expected binary locations ──
BINS = {
    "bootloader":  "selector/build/bootloader/bootloader.bin",
    "partitions":  "partitions_dualboot.bin",       # generated below
    "selector":    "selector/build/heltec_v4_selector.bin",
    "meshcore":    "meshcore/firmware.bin",          # place MeshCore bin here
    "meshtastic":  "meshtastic/firmware.bin",        # place Meshtastic bin here
}

CHIP = "esp32s3"
BAUD = "921600"
FLASH_MODE = "dio"
FLASH_FREQ = "80m"
FLASH_SIZE = "16MB"


def run(cmd, desc):
    print(f"\n{'='*60}")
    print(f"  {desc}")
    print(f"{'='*60}")
    print(f"  CMD: {' '.join(cmd)}\n")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print(f"\n❌ FAILED: {desc}")
        sys.exit(1)
    print(f"✅ Done: {desc}")


def check_file(path, label):
    if not os.path.exists(path):
        print(f"❌ Missing: {path}  ({label})")
        return False
    size = os.path.getsize(path)
    print(f"  ✅ {label}: {path}  ({size/1024:.1f} KB)")
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    port = sys.argv[1]
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    print("\n🔧 Heltec V4 Dual-Boot Flash Tool")
    print(f"   Port: {port}")
    print(f"   Chip: {CHIP}\n")

    # Step 1: Build selector
    print("Step 1: Building selector firmware...")
    os.chdir("selector")
    run(["idf.py", "build"], "Build selector app")
    os.chdir("..")

    # Step 2: Generate partitions.bin from CSV
    print("\nStep 2: Generating partition table binary...")
    run([
        "python", "-m", "esptool",
        "partition_table",
        "--flash-size", FLASH_SIZE,
        "partitions_dualboot.csv",
        "partitions_dualboot.bin"
    ], "Generate partitions.bin")

    # Fallback: use gen_esp32part.py directly if above fails
    if not os.path.exists("partitions_dualboot.bin"):
        idf_path = os.environ.get("IDF_PATH", "")
        gen_script = os.path.join(idf_path, "components", "partition_table", "gen_esp32part.py")
        run([
            "python", gen_script,
            "--flash-size", FLASH_SIZE,
            "partitions_dualboot.csv",
            "partitions_dualboot.bin"
        ], "Generate partitions.bin (fallback)")

    # Step 3: Check all binaries exist
    print("\nStep 3: Checking binaries...")
    ok = True
    for label, path in BINS.items():
        ok = check_file(path, label) and ok

    if not ok:
        print("\n❌ Some binaries are missing.")
        print("   - Build MeshCore and copy firmware.bin to meshcore/firmware.bin")
        print("   - Download Meshtastic firmware and copy to meshtastic/firmware.bin")
        print("   - Re-run this script after placing the binaries.")
        sys.exit(1)

    # Step 4: Flash everything
    print("\nStep 4: Flashing all partitions...")
    print("   ⚠️  Put device in bootloader mode:")
    print("   Hold PRG button → connect USB → release PRG\n")
    input("   Press ENTER when ready...")

    run([
        "esptool.py",
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD,
        "write_flash",
        "--flash_mode", FLASH_MODE,
        "--flash_freq", FLASH_FREQ,
        "--flash_size", FLASH_SIZE,
        OFFSETS["bootloader"],  BINS["bootloader"],
        OFFSETS["partitions"],  BINS["partitions"],
        OFFSETS["selector"],    BINS["selector"],
        OFFSETS["meshcore"],    BINS["meshcore"],
        OFFSETS["meshtastic"],  BINS["meshtastic"],
    ], "Flash all partitions")

    print("\n" + "="*60)
    print("  🎉 Flash complete!")
    print("  Press RST button on the Heltec V4 to boot.")
    print("  The selector will appear on the OLED.")
    print("  PRG button cycles between MeshCore / Meshtastic.")
    print("  Auto-boots in 5 seconds if no button pressed.")
    print("="*60 + "\n")


if __name__ == "__main__":
    main()
