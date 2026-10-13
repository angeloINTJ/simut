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

The radio goes last in that walk (ORDER) because its bus comes up late, and it does not
choose its block; the SDK does:
  - The bus is not up after cyw43_arch_init( ), which initVariant( ) of the Pico W
    variants runs. The first use brings it up (cyw43_ensure_up( ) -> cyw43_ll_bus_init( ),
    pico-sdk lib/cyw43-driver/src/cyw43_ctrl.c), and in the SIMUT the first use is the
    WiFi.mode( WIFI_STA ) of NetworkManager::begin( ), which AppManager_Boot.cpp calls
    after the buzzer's begin( ) and after loadAndCalibrateSensors( ) (DS18B20, DHT22,
    BME280). Bluetooth comes up after the Wi-Fi.
  - cyw43_spi_init( ) (pico_cyw43_driver/cyw43_bus_pio_spi.c) takes the block with
    pio_claim_free_sm_and_add_program_for_gpio_range( ) (hardware_pio/pio.c), which walks
    the blocks from the highest down and keeps the first with a free SM and room for the
    program: pio1, then pio0 on an RP2040; pio2, pio1, then pio0 on an RP2350.

The sizes in PROGRAMS were counted from the programs' own sources on 2026-10-12.
program_lengths( ) reads them again, and tools/test_pio_budget.py holds the table to
them wherever those sources exist, so a library update that grows a program fails
there and not as a full block on a board. The pins in PINS are held the same way: the
test asks the host preprocessor for the masks src/simut_config.h checks
(SIMUT_PINS_DISPLAY, SIMUT_PINS_ENC, SIMUT_PINS_BUZZER) and compares them with the table.

Left out on purpose: the BME280's PIO I2C (WirePIO, `.length = 32` in its i2c.pio.h). That
is a whole block, so it fits only in a pio0 that is empty: the driver (BME280Driver.h) calls
beginPIO( ), whose default is pio0, and when pio0 holds anything it falls back to plain
GPIO. A build that cannot give it the block still works, so it never makes a build fail,
and counting it would refuse builds that work. It is also why the wired network has to
come up before the sensors: the ENC28J60 can have pio0 as its only room (in the SIMUT Wire
the buzzer and the DHT22 leave pio1 with 11 instructions free, and the ENC28J60 needs 17),
and a BME280 that reached pio0 first would take the whole block.

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
    # pico-sdk, src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.pio, 6 instructions. The
    # SDK picks its block, not the firmware: cyw43_spi_init( ) (cyw43_bus_pio_spi.c) calls
    # pio_claim_free_sm_and_add_program_for_gpio_range( ) (hardware_pio/pio.c), whose loop
    # `int pio_num = NUM_PIOS; while (pio_num--)` goes from the highest block down and keeps
    # the first with a free SM and room for the program. So the list runs pio2, pio1, pio0,
    # and _may_use( ) drops the block a chip lacks: an RP2040 tries pio1, then pio0.
    "cyw43": {"instr": 6, "sm": 1, "blocks": ["pio2", "pio1", "pio0"]},
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

# The BME280's PIO I2C stays out of PROGRAMS (the docstring says why), but the reason it
# stays out is its size, a whole block, so that size is held to its source as well. The
# TwoWirePIO_RP2040 library installs as WirePIO under the libdeps folder.
WIREPIO_FILE = "WirePIO/src/i2c.pio.h"
WIREPIO_INSTR = 32

# The GPIOs each owner takes, from section 10b of src/simut_config.h (the SIMUT_PINS_*
# masks and the pins they are made of). The display keys are the ids of the [display.*]
# tables of tools/features.toml; tools/test_pio_budget.py holds this table to both.
#   tft             SPI0 MISO 16, touch CS 17, SCK 18, MOSI 19, touch IRQ 20; then the
#                   panel's RST 26, DC 27 and CS 28
#   alpha_paralelo  HD44780 in 4-bit parallel: RS 16, EN 17, D4..D7 18..21
#   nenhum          no display, no pin
#   lan             the ENC28J60 (an SPI in PIO): SIMUT_ENC_MISO 12, _CS 13, _SCK 14, _MOSI 15; its
#                   INT pin is separate (ENC_INT_DEFAULT, or enc_int_pin in the config)
#   buzzer          BUZZER_PIN 22
PINS = {
    "tft": (16, 17, 18, 19, 20, 26, 27, 28),
    "alpha_paralelo": (16, 17, 18, 19, 20, 21),
    "nenhum": (),
    "lan": (12, 13, 14, 15),
    "buzzer": (22,),
}
DISPLAYS = ("tft", "alpha_paralelo", "nenhum")

# SIMUT_ENC_INT_PIN when a build does not set it (section 10b of src/simut_config.h): GP11
# with every display, one wiring for the TFT and the 16x2 LCD alike, at the cost of sensor
# slot 11. -1 is no INT pin, the driver polls the module.
ENC_INT_DEFAULT = 11
GPIO_MAX = 29      # GP0..GP29, the pins an RP2040 exposes


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

def _enc_int_pin(value):
    """The GPIO of the ENC28J60's INT line, or -1 for none. None is "the config does not
    say", so the default of simut_config.h (ENC_INT_DEFAULT, the same with every display);
    -1 is "no INT pin, the driver polls"; anything else has to be a GPIO, an int from 0 to
    GPIO_MAX (a bool is not one: True would be GP1 and False GP0)."""
    if value is None:
        return ENC_INT_DEFAULT
    if isinstance(value, int) and not isinstance(value, bool) and (value == -1 or 0 <= value <= GPIO_MAX):
        return value
    raise ValueError(f"enc_int_pin must be None (the default, GP{ENC_INT_DEFAULT}), -1 (no INT pin) or a "
                     f"GPIO from 0 to {GPIO_MAX}, not {value!r}")


def pin_owners(cfg):
    """{owner: set of GPIOs} for what `cfg` switches on. Keys: display (an id of the
    [display.*] tables of tools/features.toml: "tft", "alpha_paralelo" or "nenhum"), lan
    (bool), enc_int_pin (None, the INT pin simut_config.h gives every build; -1, no INT pin
    and the driver polls; or a GPIO from 0 to GPIO_MAX) and sound_buzzer (bool). The LAN
    claims its INT pin only when the LAN is on and there is such a pin. A display or an
    enc_int_pin it does not know is a ValueError, whether or not the LAN is on."""
    kind = cfg.get("display") or "nenhum"
    if kind not in DISPLAYS:
        raise ValueError(f"unknown display {kind!r}; known: {', '.join(DISPLAYS)}")
    int_pin = _enc_int_pin(cfg.get("enc_int_pin"))
    owners = {"display:" + kind: set(PINS[kind])}
    if cfg.get("lan"):
        pins = set(PINS["lan"])
        if int_pin >= 0:
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
# What the configurator and CI read
# ----------------------------------------------------------------------------------

# The switches of ORDER a person can turn off. The radio is not one: it is on exactly when
# the cable is off, so a config from tools/gen_features.py config_of( ) has no "radio" key.
BUDGET_KEYS = tuple(key for _, key in ORDER if key != "radio")

# The switch each pin owner of pin_owners( ) stands for; a display is chosen, not switched.
_OWNER_KEY = {"lan": "lan", "buzzer": "sound_buzzer"}

# Every id refusals( ) can return. tools/gen_features.py wants words for each one in the
# manifest ([ui_budget.<id>]) before it generates, including the ones no image hits today.
REFUSAL_IDS = ("pio-budget", "pin-conflict")


def _walk(cfg):
    """The config place( ) walks: `cfg` with the radio following the cable."""
    walk = dict(cfg)
    walk["radio"] = not cfg.get("lan")
    return walk


def refusals(cfg, chip, programs=None):
    """Why a build of `cfg` on `chip` cannot run, as a list the page and CI both read: []
    when it fits. Each item is {"id", "reason", "fix"}:

    - "pio-budget": place( ) finds no room, with its sentence as the reason. `fix` is every
      switch of BUDGET_KEYS that is on and whose turning off ALONE makes it fit, in that
      order; when none alone does, every one of them that is on.
    - "pin-conflict": one per GPIO two owners claim (pin_conflicts( )), with the switches of
      those owners as `fix`, in BUDGET_KEYS order, and the GPIO as "pin", so the words the
      page shows can say which one.

    `cfg` is what tools/gen_features.py config_of( ) gives (the display, and a bool per
    switch); it is not modified. The budget comes first: it is the one a person meets on a
    real config, and a pin conflict needs a pin table the firmware would refuse to compile."""
    out = []
    r = place(_walk(cfg), chip, programs)
    if not r["ok"]:
        on = [k for k in BUDGET_KEYS if cfg.get(k)]
        fix = [k for k in on if place(_walk(dict(cfg, **{k: False})), chip, programs)["ok"]]
        out.append({"id": "pio-budget", "reason": r["reason"], "fix": fix or on})
    for pin, a, b in pin_conflicts(cfg):
        keys = {_OWNER_KEY[o] for o in (a, b) if o in _OWNER_KEY}
        out.append({"id": "pin-conflict", "reason": f"GP{pin} is claimed by {a} and by {b}.",
                    "fix": [k for k in BUDGET_KEYS if k in keys], "pin": pin})
    return out


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
