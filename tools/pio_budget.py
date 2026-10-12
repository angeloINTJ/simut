#!/usr/bin/env python3
"""The PIO budget of a build: does a set of features fit the PIO blocks, and do two of
them want the same GPIO pin?

An RP2040 has two PIO blocks ("pio0", "pio1") and an RP2350 three (and "pio2"). Each
block holds 32 instructions and 4 state machines (SMs). Several features load a PIO
program at boot, one after another, and each program goes in a block it may use that
still has room for its instructions and its SMs. When none has, the firmware says
"pio0 full" in the boot log, which whoever composed the build reads too late. This
module is the table of those programs and the walk the boot makes over it, so the
configurator and CI can say before compiling that a combination does not fit (place)
and that two features want one pin (pin_conflicts).

The sizes in PROGRAMS were counted from the programs' own sources on 2026-10-12.
program_lengths( ) reads them again, and tools/test_pio_budget.py holds the table to
them wherever those sources exist, so a library update that grows a program fails
there and not as a full block on a board.

Left out on purpose: the BME280's PIO I2C (WirePIO, 31 instructions). It takes a block
only when one is free and falls back to plain GPIO, so it never makes a build fail,
and counting it would refuse builds that work.

Standard library only.
"""
import copy
import itertools
import os
import re

BLOCK_INSTR = 32   # instruction slots in one PIO block
BLOCK_SM = 4       # state machines in one PIO block

# The blocks of each chip, in the order a program that may go anywhere tries them.
CHIPS = {
    "rp2040": ("pio0", "pio1"),
    "rp2350": ("pio0", "pio1", "pio2"),
}

# What each program takes, counted from its source on 2026-10-12. "blocks" is where the
# firmware lets it go, in the order it tries them; None means any block of the chip.
PROGRAMS = {
    # BuzzerPIO_RP2040/src/BuzzerPIO_RP2040.cpp, `.length = 4`. It asks for pio1 first
    # (BUZZER_PIO_BLOCK, src/SoundManager.h:80) and settles for pio0.
    "buzzer": {"instr": 4, "sm": 2, "blocks": ["pio1", "pio0"]},
    # The wired-LAN chip: the PicoENC28J60 library, src/enc28j60-spi.pio.h, `.length = 17`.
    # It has no block of its own, and it boots before the sensors: left to take the first
    # block with room it would take pio0 and leave the DS18B20 (pio0 only) 15 instructions
    # where it needs 27. place( ) looks ahead instead.
    "enc28j60": {"instr": 17, "sm": 1, "blocks": None},
    # DS18B20: OneWirePIO_RP2040/src/onewire_protocol.pio.h, `.length = 27`. The driver
    # builds its bus on pio0 and nowhere else (src/sensors/DS18B20Driver.h:39).
    "onewire": {"instr": 27, "sm": 1, "blocks": ["pio0"]},
    # DHT22: DHT22PIO_RP2040/src/dht22_protocol.pio.h, `.length = 17`. pio1 only
    # (src/sensors/DHT22Driver.h:41).
    "dht22": {"instr": 17, "sm": 1, "blocks": ["pio1"]},
    # The Wi-Fi radio, on the boards that have one: the program spi_gap01_sample0 of the
    # pico-sdk, src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.pio, 6 instructions,
    # on pio1.
    "cyw43": {"instr": 6, "sm": 1, "blocks": ["pio1"]},
}

# The order the boot claims them in, with the config key that switches each one on. A
# key a config does not have is off.
ORDER = (
    ("buzzer", "sound_buzzer"),
    ("enc28j60", "lan"),
    ("onewire", "sensor_ds18b20"),
    ("dht22", "sensor_dht22"),
    ("cyw43", "radio"),
)

# Where program_lengths( ) finds three of the programs, under the libdeps folder of an
# environment (.pio/libdeps/pico_w_release). The other two come from the caller: the
# ENC28J60 library is not a PlatformIO dependency of the Pico W images, and the radio's
# program lives in the framework's pico-sdk.
LIBDEPS_FILES = {
    "onewire": "OneWirePIO_RP2040/src/onewire_protocol.pio.h",
    "dht22": "DHT22PIO_RP2040/src/dht22_protocol.pio.h",
    "buzzer": "BuzzerPIO_RP2040/src/BuzzerPIO_RP2040.cpp",
}
CYW43_PROGRAM = "spi_gap01_sample0"

# The GPIOs each owner takes, from src/simut_config.h unless said otherwise:
#   tft             SPI0 MISO 16, touch CS 17, SCK 18, MOSI 19, touch IRQ 20; then the
#                   panel's RST 26, DC 27 and CS 28
#   alpha_parallel  HD44780 in 4-bit parallel: RS 16, EN 17, D4..D7 18..21
#   alpha_i2c       HD44780 backpack on I2C1: SDA 26, SCL 27
#   lan             the ENC28J60 on SPI1, wired to the group GP12..GP15; its INT pin is
#                   separate and comes from the config (enc_int_pin)
#   buzzer          BUZZER_PIN 22
PINS = {
    "tft": (16, 17, 18, 19, 20, 26, 27, 28),
    "alpha_parallel": (16, 17, 18, 19, 20, 21),
    "alpha_i2c": (26, 27),
    "none": (),
    "lan": (12, 13, 14, 15),
    "buzzer": (22,),
}
DISPLAYS = ("tft", "alpha_parallel", "alpha_i2c", "none")


# ----------------------------------------------------------------------------------
# Placement
# ----------------------------------------------------------------------------------

def _plural(n, noun):
    return f"{n} {noun}" + ("" if n == 1 else "s")


def _amount(instr, sm):
    return f"{_plural(instr, 'instruction')} and {_plural(sm, 'state machine')}"


def _free(blocks, block):
    used = blocks[block]
    return BLOCK_INSTR - used["instr"], BLOCK_SM - used["sm"]


def _free_text(blocks, block):
    return f"{block} has {_amount(*_free(blocks, block))} free"


def _fits(blocks, block, prog):
    instr, sm = _free(blocks, block)
    return prog["instr"] <= instr and prog["sm"] <= sm


def _take(blocks, block, prog):
    blocks[block]["instr"] += prog["instr"]
    blocks[block]["sm"] += prog["sm"]


def _may_use(prog, blocks):
    """The blocks of this chip a program may go in, in the order it tries them."""
    if prog["blocks"] is None:
        return list(blocks)
    return [b for b in prog["blocks"] if b in blocks]


def _first_fit(blocks, prog):
    """The first block the program may use with room for its instructions and its SMs."""
    for b in _may_use(prog, blocks):
        if _fits(blocks, b, prog):
            return b
    return None


def _first_misfit(trial, later, table):
    """Put the `later` programs (names) in `trial`, a scratch copy, each by first fit.
    Returns the name of the first that finds no room, or None when all of them fit."""
    for name in later:
        where = _first_fit(trial, table[name])
        if where is None:
            return name
        _take(trial, where, table[name])
    return None


def _no_room(name, prog, blocks):
    need = _amount(prog["instr"], prog["sm"])
    tried = _may_use(prog, blocks)
    if not tried:
        return (f"{name} needs {need}, but this chip has none of the blocks it can use "
                f"({', '.join(prog['blocks'])}).")
    free = "; ".join(_free_text(blocks, b) for b in tried)
    return f"{name} needs {need} and no block it can use has that much free: {free}."


def _no_room_ahead(name, prog, blocks, why):
    """`why` maps each block to the name of the later program that would not fit if
    `name` went there, or to None when `name` does not fit in it at all."""
    parts = []
    for b in blocks:
        text = _free_text(blocks, b)
        if why[b] is None:
            text += ", not enough for it"
        else:
            text += f", but then {why[b]} would not fit"
        parts.append(text)
    need = _amount(prog["instr"], prog["sm"])
    return (f"{name} needs {need} and finds no block where it leaves room for the programs "
            f"that load after it: " + "; ".join(parts) + ".")


def _result(ok, blocks, enc_block, reason=""):
    return {"ok": ok, "blocks": blocks, "enc_block": enc_block, "reason": reason}


def place(cfg, chip, programs=None):
    """Walk the boot's claims over the PIO blocks of `chip` for the features `cfg` turns on.

    Returns {"ok", "blocks", "enc_block", "reason"}: "blocks" maps every block of the chip
    to {"instr": used, "sm": used}; "enc_block" is where the enc28j60 went (None when the
    LAN is off); "reason" is "" when everything fits and one sentence when something does
    not, in which case "blocks" is as the walk left it when it stopped.

    1. ORDER is walked as the boot walks it, skipping what the config switches off.
    2. A program with a list of blocks goes in the first one with room for both its
       instructions and its SMs. If none has, the walk stops there.
    3. A program that may go anywhere (the enc28j60) goes in the first block of the chip,
       in order, with room for it AND such that every program that loads after it still
       finds its place by rule 2. Taking the first block with room is not enough: the
       enc28j60 boots before the sensors, and the room it takes in pio0 or pio1 is room
       a sensor that has only that one block then lacks.

    `programs` replaces the table for a what-if (the tests use it); it defaults to PROGRAMS.
    """
    table = PROGRAMS if programs is None else programs
    if chip not in CHIPS:
        raise ValueError(f"unknown chip {chip!r}; known: {', '.join(CHIPS)}")
    blocks = {b: {"instr": 0, "sm": 0} for b in CHIPS[chip]}
    wanted = [name for name, key in ORDER if cfg.get(key)]
    enc_block = None
    for i, name in enumerate(wanted):
        prog = table[name]
        if prog["blocks"] is not None:
            where = _first_fit(blocks, prog)
            if where is None:
                return _result(False, blocks, enc_block, _no_room(name, prog, blocks))
        else:
            where, why = None, {}
            for b in blocks:
                if not _fits(blocks, b, prog):
                    why[b] = None
                    continue
                trial = copy.deepcopy(blocks)
                _take(trial, b, prog)
                why[b] = _first_misfit(trial, wanted[i + 1:], table)
                if why[b] is None:
                    where = b
                    break
            if where is None:
                return _result(False, blocks, enc_block, _no_room_ahead(name, prog, blocks, why))
        _take(blocks, where, prog)
        if name == "enc28j60":
            enc_block = where
    return _result(True, blocks, enc_block)


# ----------------------------------------------------------------------------------
# Pins
# ----------------------------------------------------------------------------------

def pin_owners(cfg):
    """{owner: set of GPIOs} for what `cfg` switches on. Keys: display ("tft",
    "alpha_parallel", "alpha_i2c" or "none"), lan (bool), enc_int_pin (a GPIO, or None
    or -1 when the ENC28J60 has no INT pin and the driver polls) and sound_buzzer (bool).
    The LAN claims its INT pin only when the LAN is on and there is such a pin."""
    kind = cfg.get("display") or "none"
    if kind not in DISPLAYS:
        raise ValueError(f"unknown display {kind!r}; known: {', '.join(DISPLAYS)}")
    owners = {"display:" + kind: set(PINS[kind])}
    if cfg.get("lan"):
        pins = set(PINS["lan"])
        int_pin = cfg.get("enc_int_pin")
        if int_pin is not None and int_pin >= 0:
            pins.add(int_pin)
        owners["lan"] = pins
    if cfg.get("sound_buzzer"):
        owners["buzzer"] = set(PINS["buzzer"])
    return owners


def pin_conflicts(cfg):
    """Every GPIO that two owners claim, as (pin, owner_a, owner_b) with owner_a before
    owner_b alphabetically, sorted by pin. [] when no pin is shared."""
    owners = pin_owners(cfg)
    found = []
    for a, b in itertools.combinations(sorted(owners), 2):
        for pin in owners[a] & owners[b]:
            found.append((pin, a, b))
    return sorted(found)


# ----------------------------------------------------------------------------------
# The sources the table was counted from
# ----------------------------------------------------------------------------------

_LENGTH = re.compile(r"\.length\s*=\s*(\d+)")
_LABEL = re.compile(r"^(?:public\s+)?[A-Za-z_]\w*\s*:")


def _read(path):
    """The text of a file, or None when it does not exist. A file that exists and cannot
    be read raises: "could not look" is not "is not there"."""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except FileNotFoundError:
        return None


def _length_in(text, path):
    found = _LENGTH.findall(text)
    if not found:
        raise ValueError(f"{path}: no `.length = N` in it; the program was renamed or the "
                         f"file changed shape")
    if len(found) > 1:
        raise ValueError(f"{path}: {len(found)} `.length = N` in it; which one is the program?")
    return int(found[0])


def _count_instructions(text, program, path):
    """The instructions of `.program <program>` in a pioasm source: the lines up to the
    next `.program` or the first line starting with `%` (the C block), less comments,
    labels, directives and blank lines."""
    inside = found = False
    count = 0
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].strip()
        words = line.split()
        if words[:1] == [".program"]:
            inside = words[1:2] == [program]
            found = found or inside
            continue
        if not inside:
            continue
        if line.startswith("%"):
            break
        line = _LABEL.sub("", line).strip()
        if line and not line.startswith("."):
            count += 1
    if not found:
        raise ValueError(f"{path}: no `.program {program}` in it")
    if count == 0:
        raise ValueError(f"{path}: `.program {program}` has no instruction")
    return count


def program_lengths(libdeps, enc_header, cyw43_pio):
    """The size of each program as its own source says it, {name: instructions}.

    onewire, dht22 and buzzer are read from `libdeps` (LIBDEPS_FILES), enc28j60 from
    `enc_header`, all by their `.length = N`; cyw43 is counted from `.program
    spi_gap01_sample0` in `cyw43_pio`. A file that does not exist is left out of the
    answer. A file that exists without the pattern raises ValueError."""
    paths = {name: os.path.join(libdeps, rel) for name, rel in LIBDEPS_FILES.items()}
    paths["enc28j60"] = enc_header
    found = {}
    for name, path in paths.items():
        text = _read(path)
        if text is not None:
            found[name] = _length_in(text, path)
    text = _read(cyw43_pio)
    if text is not None:
        found["cyw43"] = _count_instructions(text, CYW43_PROGRAM, cyw43_pio)
    return found
