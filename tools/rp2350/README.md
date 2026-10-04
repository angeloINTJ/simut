# tools/rp2350/ — the Pico 2 W's flash layout

The RP2350 image boots from slot A or B of a partition table, steps 3 to 7 of
[`docs/analysis/OTA_AB_RP2350.md`](../../docs/analysis/OTA_AB_RP2350.md). The
files here say where everything is, to `picotool` and to the linker, and write
what a release publishes.

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
| `picobin.py` | An image's PICOBIN blocks as the boot ROM reads them; `mark_trial`, which sets the try-before-you-buy bit (0x8000 of IMAGE_TYPE) in the image an update installs; and `clear_trial`, which clears it the way the boot ROM does when the image buys itself. |
| `uf2.py` | The factory `.uf2`: the table as `picotool` wrote it, then the program in slot A, absolute throughout and numbered as one download. The build and the release both write it with this code. |

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
    Arm, secure, RP2350, without the try-before-you-buy bit;
  - the image carries one env tag, ending in `two` (`releasetwo`).

  Then it writes three files next to `firmware.uf2`. The factory `.uf2` comes
  from `uf2.py`, and the build fails if it differs from what `picotool`
  writes.
- **`tools/test_rp2350_trial.py`** (CI, gates job): `mark_trial` changes that
  one bit and nothing else, `clear_trial` undoes exactly that, and both refuse
  what they must.
- **`tools/test_release_manifest.py`** (CI, gates job): the release's two files
  for the Pico 2 W, written on synthetic images; see "In a release" below.

## What a build leaves in `.pio/build/pico2_w_release/`

- **`firmware.uf2`**: the program alone, family `rp2350-arm-s`. On a board
  that has the table, `picotool load -p 0 firmware.uf2` puts it in slot A.
- **`partition_table.uf2`**: the table alone.
- **`firmware_factory.uf2`**: the table and the program in slot A, every block
  in the absolute family, for a board whose flash holds nothing. `picotool load`
  reads it as such. On the A2, erratum RP2350-E10 fails a drag-and-drop onto a
  flash that already holds a partition table. `picotool` works around it with a
  block in the last sector, but only in a file whose blocks go to a partition by
  family (`elf2uf2.cpp`). This file is absolute throughout and carries no such
  block, and dragging it works: on 2026-10-04 the v2.11.0 release gate dragged
  the candidate's onto the bench board, an A2 whose flash held a partition
  table, and the board came up from slot A.
- **`firmware_ota.bin`**: `firmware.bin` flagged try-before-you-buy, one byte
  apart: the image an update installs. The ROM boots it only right after the
  apply's reboot, never from USB.

## An update over the air

Steps 4 and 5 of the design. `firmware_ota.bin`, signed with
`tools/ota_sign.py`, goes through the Files page like the Pico W's, and lands in
the slot the board did not boot from (`src/ota/slot_stage.h`):

- the stage erases that slot's first sector before anything else, then what
  the image will cover, from the request's length (64 KB blocks where aligned;
  ~1 MB in 1.4 s), writes the rest as it arrives, and keeps the image's first
  4 KB in RAM;
- the image is checked: its env tag (`releasetwo`), its image definition, its
  size, its signature;
- the apply checks the signature again, writes the first sector, and reboots
  into the slot through the boot ROM (`FLASH_UPDATE`).

Until the apply the slot has no image definition, so a cut or a reset leaves
the board on the image it runs. LittleFS is not touched.

The new image boots on trial (step 5, `src/ota/trial.h`): `sys.trial` is 1 in
`/api/status` until it has been healthy for a minute and buys itself. A reset,
a power cut or the watchdog before that, or five minutes without that minute,
bring back the image it replaced, which logs code 613 and reports the version in
`sys.reverted`. After the buy, going to B the ROM erases A's first sector; going
to A it erases nothing, and B keeps the image it had. An unflagged
`firmware.bin` still installs, without a trial, as in step 4.

## In a release

Since step 7, `release-ota.yml` publishes two files for the Pico 2 W, with
`releasetwo` in the name:

- **`simut_v<version>_releasetwo.bin`**: `firmware_ota.bin`, signed by the
  release key. It is the update, flagged for trial; the manifest refuses an
  update without the flag.
- **`simut_v<version>_releasetwo.uf2`**: the factory image, written by
  `tools/release_manifest.py` with `uf2.py` from the build's
  `partition_table.uf2` and the signed update, with the trial flag cleared by
  `clear_trial`. When an update buys itself, the boot ROM clears the flag in
  flash. So a board flashed over USB holds the same bytes as a board updated
  over the air, signature trailer included. The two files differ in the table
  and in one byte, at `0x12b`.

So `pico2_w_release` trusts the release root alone, like every image a release
publishes, and the signing job would refuse it otherwise. A bench image comes
from the same profile with
`PLATFORMIO_BUILD_FLAGS=-DSIMUT_OTA_TRUST_BENCH=1 pio run -e pico2_w_release`;
on 2026-10-04 that gave, byte for byte, the image main built before step 7. A
board that runs an image without the bench root refuses everything the bench
key signs (`v=12`), so the way back from a release is USB.

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
