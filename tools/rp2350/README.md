# tools/rp2350/ — the Pico 2 W's flash layout

The RP2350 image boots from slot A of a partition table, step 3 of
[`docs/analysis/OTA_AB_RP2350.md`](../../docs/analysis/OTA_AB_RP2350.md). The
files here say where everything is, to `picotool` and to the linker.

| Region | Offset | Size |
|---|---:|---:|
| Partition table | `0x000000` | 8 KB |
| Slot A | `0x002000` | 1,532 KB |
| Slot B | `0x181000` | 1,532 KB |
| LittleFS | `0x300000` | 1,020 KB |
| arduino-pico's EEPROM sector | `0x3FF000` | 4 KB |

| File | What it is |
|---|---|
| `partition_table.json` | The table, in `picotool`'s format. **The source of the layout:** A and B are a linked pair, LittleFS and the EEPROM sector are data partitions the boot ROM skips, and unpartitioned space takes the absolute family (`picotool`'s fix for the RP2350-E10 erratum needs it, and the bench board is an A2). |
| `memmap_slot.ld` | The linker script of `pico2_w_release`. **Generated:** never edit it. |
| `gen_memmap.py` | Writes `memmap_slot.ld` from arduino-pico's `lib/rp2350/memmap_default.ld`. It drops the framework's 12 KB OTA stub, whose image definition the ROM would otherwise take and whose LittleFS read faults outside a slot, and puts in the slot's numbers from `partition_table.json`. `--check` says whether the file still is what the template gives. |

## What holds it together

- **`tools/test_rp2350_layout.py`** (CI, gates job): the table, the map in
  `src/ota/ota_layout.h`, this linker script and the PlatformIO profile say the
  same numbers.
- **`tools/check_rp2350_image.py`**, after every `pico2_w_release` link:
  - the linker script still matches the framework's template;
  - the framework carries the LittleFS patch (2k in
    `tools/arduino_pico_overrides/`);
  - the stub is gone, and the image fits its slot;
  - the first block in the image's first 4 KB is its own image definition:
    Arm, secure, RP2350, without the try-before-you-buy bit.

  Then it writes two files next to `firmware.uf2`.

## What a build leaves in `.pio/build/pico2_w_release/`

- **`firmware.uf2`**: the program alone, family `rp2350-arm-s`. On a board
  that has the table, `picotool load -p 0 firmware.uf2` puts it in slot A.
- **`partition_table.uf2`**: the table alone.
- **`firmware_factory.uf2`**: the table and the program in slot A, every block
  in the absolute family, for a board whose flash holds nothing. `picotool load`
  reads it as such. Dragging it onto the BOOTSEL drive of an A2 board has not
  been tried.

## A board on the old layout

Before step 3, `pico2_w_release` kept LittleFS at `0x2FF000`. The table moves
it 4 KB up and makes it 4 KB smaller, so a board that boots the new layout
finds no filesystem where it looks, and formats one. Its configuration, packs
and history have to come back from a backup.

The only board on the old layout was the bench's. It was migrated on
2026-10-03 without losing anything:

- `picotool save` read its whole flash, and the old LittleFS came out of that
  dump;
- littlefs-python rebuilt it in 255 blocks, with arduino-pico's parameters, and
  every file read back identical (sha256);
- one `picotool load --ignore-partitions` wrote the table, the program in
  slot A and the new LittleFS, and erased the second sector of the table's
  area and the first sector of slot B, which held old bytes.

It booted from slot A with its configuration and packs.
