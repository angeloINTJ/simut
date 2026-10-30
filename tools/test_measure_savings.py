#!/usr/bin/env python3
"""Tests the flash number the savings floor compares (tools/measure_savings.py --check, --update).

Run: python3 tools/test_measure_savings.py   (CI runs it in the gates job; the floor itself runs nightly)

The floor compares the `used` PlatformIO prints for an image with a feature on against the same image with it off,
each taken WITHOUT the fill the linker writes between input sections: every `*fill*` the map lists inside .boot2,
.text, .rodata and .data, the sections `used` adds up. Measured with the fill, a saving moves 8-16 B from
one commit to the next with no symbol moved: one saving fell below its floor on 2026-10-28 and nine of sixteen on
2026-10-29 (issue #296), because the images are padded in front of a few 8-byte-aligned sections (the 688 B
`.fini.__stub`, the `time_t` tables behind the string pools) and the pad follows the size of everything before it,
in the base and in the variant differently. The BTstack bank's fill (2026-10-22, #285) is the biggest case of the
same thing: it reaches 4,095 B, and with it the Alpha's Bluetooth saving fell 2,912 B in three weeks while the code
its switch removes grew 8 B. So:

  * flash_fill( ) sums the fills of .boot2/.text/.rodata/.data from a map shaped like the real ones. It refuses,
    rather than answer 0, a text that is no memory map, a map whose four sections do not add up to `used`, a map
    without one of them, a fill outside any output section and a fill line it cannot parse. It does not count a
    fill in a section `used` leaves out (.ota, .partition, .bss ...) nor one above the memory map.
  * pio_build( ) asks for the map, reads the fill, and refuses a build that left no map, ran without zopfli or left
    a map that does not add up: a fill nobody read is not a fill of zero.
  * measure( ) takes each side's fill off: the saving is (used - fill) of the base minus (used - fill) of the variant.
  * cmd_check( ) lets a flash drop of FLASH_TOL (8 B) pass and fails the next byte; RAM keeps its 64 B.
  * The BTstack bank of #285 is one fill among the others: the 548 B before it on the Alpha are taken off, and the
    4,096 B step the Air's tel_tls saving made does not move the number.

Each case of the floor's side carries an id ([a1], [d2] ...) that the mutations of #302 (remove the fill subtraction,
remove the tolerance, drop the self-check ...) are matched to. The 21 cases of the configurator's matrix side (the
`ui=` and `net=` flips, the Angulo's static-RAM budget in the temporary env, the checks of `--checks`) carry none.
The cases of `--matrix --only` ([i0]-[i3]: the rebuilt bases are compared with the file before a flip is built) carry
ids too, matched to the mutants of S11 B2 (2026-10-30: a mutant that dropped `used` from that comparison passed the
whole suite, because no case ran the function).
"""
import contextlib
import hashlib
import io
import json
import os
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import measure_savings as ms                        # noqa: E402

FAILS = []
RAN = []


def check(name, cond, detail=""):
    RAN.append(name)
    tag = "ok  " if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


def case(name, fn):
    """fn( ) -> (ok, detail). An exception is a failed case, not a crash: the cases run as a list."""
    try:
        ok, detail = fn()
    except Exception as e:                           # noqa: BLE001
        ok, detail = False, f"raised {type(e).__name__}: {e}"
    check(name, ok, detail)


# ---- a linker map, from entries ---------------------------------------------------------------------------------
BOOT2 = 0x100
FILL = "*fill*"
# The end of .rodata of pico_w_alpha on main 25e169df, as the linker wrote it (-Wl,-Map). The three SPI sections
# share an address: merged strings, so "the end of the entry before" is not a rule.
REAL_TAIL = """\
 .rodata.btstack_link_key_db_tlv
                0x100edd64       0x24 /x/liblwip-bt.a(btstack_link_key_db_tlv.c.o)
 .rodata._ZN14SPIClassRP20405setRXEh.str1.1
                0x100edd88       0x67 .pio/build/pico_w_alpha/lib530/libSPI.a(SPI.cpp.o)
 .rodata._ZN14SPIClassRP20405beginEb.str1.1
                0x100edd88       0x23 .pio/build/pico_w_alpha/lib530/libSPI.a(SPI.cpp.o)
 .rodata._ZTV14SPIClassRP2040
                0x100edd88       0x40 .pio/build/pico_w_alpha/lib530/libSPI.a(SPI.cpp.o)
                0x100edd88                _ZTV14SPIClassRP2040
 .rodata._ZL28pico_flash_bank_instance_obj
                0x100eddc8       0x14 .pio/build/pico_w_alpha/libFrameworkArduino.a(btstack_flash_bank.cpp.o)
 *fill*         0x100edddc      0x224
 .rodata._ZL15__bluetooth_tlv
                0x100ee000     0x2000 .pio/build/pico_w_alpha/libFrameworkArduino.a(btstack_flash_bank.cpp.o)
                0x100f0000                        . = ALIGN (0x4)
 *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
                0x100f0000                        . = ALIGN (0x4)
"""
REAL_RODATA_SIZE = 0x6a000                           # .rodata 0x10086000 + 0x6a000 = 0x100f0000, the end above


def _section(name, addr, entries, size=None, load=None, raw=None):
    """The lines of one output section. `entries`: (input section name, bytes) or (FILL, bytes); `raw` is
    the body verbatim (a real tail). A name of 15 characters or more takes two lines, as ld writes it."""
    total = size if size is not None else sum(sz for _, sz in entries)
    tail = f" load address {load:#x}" if load is not None else ""
    if len(name) < 15:
        lines = [f"{name:<15} 0x{addr:08x} {total:#10x}{tail}"]
    else:
        lines = [name, f"{'':<16}0x{addr:08x} {total:#10x}{tail}"]
    lines.append(f" *({name})")
    if raw is not None:
        return lines + raw.rstrip("\n").split("\n"), total
    a = addr
    for nm, sz in entries:
        if nm == FILL:
            lines.append(f" {FILL:<15}0x{a:08x} {sz:#10x} ")
        elif len(nm) < 15:
            lines.append(f" {nm:<15}0x{a:08x} {sz:#10x} /x/o.a(o.cpp.o)")
        else:
            lines += [f" {nm}", f"{'':<16}0x{a:08x} {sz:#10x} /x/o.a(o.cpp.o)"]
        if ".str1." not in nm:          # merged strings share addresses, as in the real maps (REAL_TAIL)
            a += sz
    lines.append(f"{'':<16}0x{a:08x}{'':<24}. = ALIGN (0x4)")
    return lines, total


def mk_map(text=(), rodata=(), data=(), ota=(), bss=(), vec=(), rodata_raw=None, rodata_size=None,
           used=None, discarded="", skip=()):
    """(map text, used): a linker map shaped like the real ones, and the `used` its .boot2 + .text + .rodata
    + .data add up to. With `used`, a filler input section in .text makes the map add up to that number.
    `ota`, `bss` and `vec` (a section whose name takes two lines) are sections `used` does not count."""
    text = list(text)
    ro_total = rodata_size if rodata_size is not None else sum(sz for _, sz in rodata)
    da_total = sum(sz for _, sz in data)
    if used is not None:
        text.append((".text.filler", used - BOOT2 - ro_total - da_total - sum(sz for _, sz in text)))
    out = [discarded, "Linker script and memory map", "", "LOAD /x/crt0.o",
           f"{'.flash_begin':<15} 0x10000000        0x0"]
    parts = [(".boot2", 0x10000000, [(".boot2", BOOT2)], None, None, None),
             (".ota", 0x10000100, list(ota), None, None, None),
             (".text", 0x10003000, text, None, None, None),
             (".rodata", 0x10086000, list(rodata), rodata_size, None, rodata_raw),
             (".ram_vector_table", 0x20000000, list(vec), None, None, None),
             (".data", 0x200000c0, list(data), None, 0x100f0000, None),
             (".bss", 0x20001f00, list(bss), None, 0x100f0100, None)]
    for name, addr, entries, size, load, raw in parts:
        if name in skip:
            continue
        lines, _ = _section(name, addr, entries, size, load, raw)
        out += lines
    total = BOOT2 + sum(sz for _, sz in text) + ro_total + da_total
    return "\n".join(out) + "\n", total


def fill_of(text, used):
    """flash_fill( ), with a refusal as a value: a case that should give a number and raises fails as a case."""
    try:
        return ms.flash_fill(text, used)
    except ValueError as e:
        return f"refused ({e})"
    except Exception as e:                           # noqa: BLE001  (the API may not exist yet)
        return f"error ({type(e).__name__}: {e})"


def refused(x):
    return isinstance(x, str) and x.startswith("refused")


def content(text, used):
    """What the floor takes of one image: `used` without the fill. None when the fill could not be read, so a
    case never compares two refusals as if they were two numbers."""
    f = fill_of(text, used)
    return None if isinstance(f, str) else used - f


# ---- reading the fills --------------------------------------------------------------------------------------------
def reading_cases():
    txt, used = mk_map(text=[(".text.a", 0x40), (FILL, 2), (".text.b", 0x20)], rodata=[(".rodata.a", 0x30)],
                       data=[(".data", 8)])
    check("[f5] a map with a fill only in .text: 2", fill_of(txt, used) == 2, fill_of(txt, used))
    nofill, used0 = mk_map(text=[(".text.a", 0x40)], rodata=[(".rodata.a", 0x30)], data=[(".data", 8)])
    check("[f2] an image with no fill: 0", fill_of(nofill, used0) == 0, fill_of(nofill, used0))

    allthree, used3 = mk_map(text=[(".text.a", 0x40), (FILL, 2), (".text.b", 0x20), (FILL, 4)],
                             rodata=[(".rodata.a", 0x30), (FILL, 3)], data=[(".data", 8), (FILL, 1)])
    check("[f2] the fills of .text, .rodata and .data all count: 2+4+3+1", fill_of(allthree, used3) == 10,
          fill_of(allthree, used3))

    # [f1] a fill in a section `used` does not add up is not part of the number
    other, usedo = mk_map(text=[(".text.a", 0x40)], rodata=[(".rodata.a", 0x30)], data=[(".data", 8)],
                          ota=[(".ota.x", 0x40), (FILL, 6)], bss=[(".bss.a", 0x20), (FILL, 12)],
                          vec=[(".ram_vector_table", 0xc0), (FILL, 5)])
    check("[f1] fills in .ota, .bss and a two-line-header section are not counted: 0",
          fill_of(other, usedo) == 0, fill_of(other, usedo))

    # [f3] above the memory map there is "Discarded input sections": nothing there is in the image
    discarded = "Discarded input sections\n\n .text          0x00000000        0x0 /x/crt0.o\n" \
                " *fill*         0x00000000       0x20 \n\n"
    above, usedv = mk_map(text=[(".text.a", 0x40), (FILL, 2)], rodata=[(".rodata.a", 0x30)],
                          data=[(".data", 8)], discarded=discarded)
    check("[f3] a fill above the memory map is not counted: 2", fill_of(above, usedv) == 2, fill_of(above, usedv))

    # [f4] merged strings (.str1.1) share addresses, so a real .rodata lists MORE than its header: not refused
    merged, usedm = mk_map(text=[(".text.a", 0x40)], rodata_size=150, data=[(".data", 8)],
                           rodata=[(".rodata.str1.1", 100), (".rodata._ZN3Foo3barEv.str1.1", 80),
                                   (".rodata.t", 8), (FILL, 4)])
    check("[f4] a .rodata whose entries add up to more than its header is read: 4",
          fill_of(merged, usedm) == 4, fill_of(merged, usedm))

    # zero-size entries and fills are harmless
    zero, usedz = mk_map(text=[(".text.a", 0x40), (".text.empty", 0), (FILL, 0)], rodata=[(".rodata.a", 0x30)],
                         data=[(".data", 8)])
    check("[f6] zero-size entries and a zero fill: 0", fill_of(zero, usedz) == 0, fill_of(zero, usedz))


def refusal_cases():
    txt, used = mk_map(text=[(".text.a", 0x40), (FILL, 2)], rodata=[(".rodata.a", 0x20)], data=[(".data", 8)])
    check("[c0] a map that adds up reads (the control of the refusals below)", fill_of(txt, used) == 2, fill_of(txt, used))
    more, less = fill_of(txt, used + 8), fill_of(txt, used - 8)
    check("[c1] a map whose four sections add up to 8 B less than `used` is refused, not 0", refused(more), more)
    check("[c1] a map whose four sections add up to 8 B more than `used` is refused, not 0", refused(less), less)

    norodata, _ = mk_map(text=[(".text.a", 0x40), (FILL, 2)], data=[(".data", 8)], skip=(".rodata",))
    used_norodata = BOOT2 + 0x42 + 8                     # adds up WITHOUT .rodata, so the sum is not what refuses
    got = fill_of(norodata, used_norodata)
    check("[c2] a map without .rodata is refused, not read as 0", refused(got), got)

    outside = txt.replace("LOAD /x/crt0.o\n", f"LOAD /x/crt0.o\n {FILL:<15}0x10000000        0x2 \n", 1)
    got = fill_of(outside, used)
    check("[c3] a fill outside any output section is refused", refused(got), got)

    garbled = txt.replace(f" {FILL:<15}0x", f" {FILL:<15}0y", 1)
    got = fill_of(garbled, used)
    check("[c4] a fill line this reader cannot parse is refused, not skipped", refused(got), got)

    got = fill_of(REAL_TAIL, 1)
    check("[c5] a text with no memory map is refused, not 0", refused(got), got)

    toobig, usedtb = mk_map(text=[(".text.a", 0x10), (FILL, 0x40)], rodata=[(".rodata.a", 0x20)], data=[(".data", 8)],
                            rodata_size=0x20)
    text_bigfill = toobig.replace(f"{'.text':<15} 0x10003000 {0x50:#10x}", f"{'.text':<15} 0x10003000 {0x20:#10x}")
    got = fill_of(text_bigfill, BOOT2 + 0x20 + 0x20 + 8)
    check("[c7] a fill bigger than its own section is refused", refused(got), got)


# ---- the Alpha's bank, #285 ---------------------------------------------------------------------------------------
def bank_cases():
    real, used = mk_map(rodata_raw=REAL_TAIL, rodata_size=REAL_RODATA_SIZE, text=[(".text.a", 0x1000)],
                        data=[(".data", 0x40)])
    got = fill_of(real, used)
    check("[e1] the real tail of the Alpha's .rodata: the 548 B before the BTstack bank", got == 548, got)

    renamed = [(".rodata.a", 0x100), (FILL, 0x224), (".rodata._ZL15__bluetooth_bank", 0x2000)]
    txt, used = mk_map(text=[(".text.a", 0x40)], rodata=renamed, data=[(".data", 8)])
    got = fill_of(txt, used)
    check("[e4] a bank a framework renamed: its fill is taken off all the same, no refusal", got == 548, got)

    up = [(FILL, 0x10), (".rodata.a", 0x400), (".rodata._ZL15__bluetooth_tlv", 0x2000)]
    txt, used = mk_map(text=[(".text.a", 0x40)], rodata=up, data=[(".data", 8)])
    got = fill_of(txt, used)
    check("[e5] a fill further up than the bank is a fill like any other: 16 (it was 0 for bank_pad)",
          got == 16, got)


# ---- the fill is not part of the number ----------------------------------------------------------------------------
def fill_not_in_number_cases():
    same = [(".text.a", 0x40), (".text.b", 0x20), (".text.c", 0x10)]
    m1, u1 = mk_map(text=[same[0], same[1], (FILL, 2), same[2], (FILL, 4)],
                    rodata=[(".rodata.a", 0x30), (FILL, 3), (".rodata.b", 0x10), (FILL, 4)], data=[(".data", 8)])
    m2, u2 = mk_map(text=same, rodata=[(".rodata.a", 0x30), (".rodata.b", 0x10), (FILL, 1)], data=[(".data", 8)])
    c1, c2 = content(m1, u1), content(m2, u2)
    check("[a1] two maps with the same input sections and different fills: the same `used - fill`",
          c1 is not None and u1 != u2 and c1 == c2, (u1, u2, c1, c2))

    # [b1] a symbol the switch removed that now stays: the saving falls by its size, whatever the fills do
    lineF = (".text._ZN11PromMetrics5lineFEPcjPKcS2_di", 68)
    keep = [(".text.keep", 0x100), (FILL, 2)]
    mb, ub = mk_map(text=keep + [lineF, (FILL, 4)], rodata=[(".rodata.a", 0x20)], data=[(".data", 8)])
    mo, uo = mk_map(text=keep + [(FILL, 4)], rodata=[(".rodata.a", 0x20)], data=[(".data", 8)])
    ml, ul = mk_map(text=keep + [lineF], rodata=[(".rodata.a", 0x20)], data=[(".data", 8)])
    cb, co, cl = content(mb, ub), content(mo, uo), content(ml, ul)
    got = None if None in (cb, co, cl) else (cb - co, cb - cl)
    check("[b1] a 68 B symbol the switch removes saves 68 B; kept in the variant it saves 0",
          got == (68, 0), got)


# ---- the gate: the tolerance and the findings -----------------------------------------------------------------------
def gate_cases():
    ledger = {"web_metrics": {"base": "pico_w_release", "flash_saved": 4955, "ram_saved": 0},
              "web_https": {"base": "pico_w_release", "flash_saved": 24546, "ram_saved": 16}}
    saved = ms.LEDGER
    with tempfile.TemporaryDirectory() as d:
        ms.LEDGER = os.path.join(d, "feature_savings.json")
        with open(ms.LEDGER, "w") as fh:
            json.dump(ledger, fh)

        def run(results):
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                rc = ms.cmd_check(results, list(results))
            return rc, err.getvalue()

        def one(flash, ram=0):
            return {"web_metrics": {"base": "pico_w_release", "flash_saved": flash, "ram_saved": ram}}

        try:
            case("[d1] a flash drop of 8 B passes", lambda: ((lambda r: (r[0] == 0, r))(run(one(4955 - 8)))))
            case("[d2] a flash drop of 9 B fails, and the message names the feature and the tolerance",
                 lambda: ((lambda r: (r[0] == 1 and "web_metrics" in r[1] and f"tolerancia {ms.FLASH_TOL} B" in r[1], r))
                          (run(one(4955 - 9)))))
            case("[d3] a rise and an unchanged number pass",
                 lambda: (run(one(4955))[0] == 0 and run(one(5100))[0] == 0, None))
            case("[d4] the RAM tolerance is still 64 B: 64 B below the floor passes, 65 B fails",
                 lambda: (run(one(4955, ram=-64))[0] == 0 and run(one(4955, ram=-65))[0] == 1, None))

            def exactly_one():
                res = {"web_metrics": {"base": "pico_w_release", "flash_saved": 4955 - 210, "ram_saved": 0},
                       "web_https": {"base": "pico_w_release", "flash_saved": 24546 - 4, "ram_saved": 16}}
                rc, err = run(res)
                findings = [ln for ln in err.splitlines() if ln.startswith("  ")]
                return rc == 1 and len(findings) == 1 and "web_metrics" in findings[0], (rc, findings)
            case("[b2] a 210 B leak and a 4 B drift of another feature: exactly one finding, the leak", exactly_one)
        finally:
            ms.LEDGER = saved


# ---- the builds ---------------------------------------------------------------------------------------------------------
ALPHA_USED = 976236
SIZES_OUT = ("RAM:   [=====     ]  47.9% (used 125680 bytes from 262144 bytes)\n"
             f"Flash: [========= ]  93.5% (used {ALPHA_USED} bytes from 1044480 bytes)\n")
ALPHA_MAP, _ = mk_map(rodata_raw=REAL_TAIL, rodata_size=REAL_RODATA_SIZE, used=ALPHA_USED, data=[(".data", 0x40)])


class FakeBuild:
    """Stands in for `pio run`: records how it was asked and writes the map it is given."""

    def __init__(self, root, stdout=SIZES_OUT, returncode=0, map_text=None, bin_bytes=None):
        self.root, self.stdout, self.rc, self.map_text = root, stdout, returncode, map_text
        self.bin_bytes = bin_bytes
        self.calls = []

    def __call__(self, env, linker_map=False):
        self.calls.append((env, linker_map))
        d = os.path.join(self.root, ".pio", "build", env)
        if self.map_text is not None:
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, "firmware.map"), "w") as fh:
                fh.write(self.map_text)
        if self.bin_bytes is not None:
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, "firmware.bin"), "wb") as fh:
                fh.write(self.bin_bytes)
        return types.SimpleNamespace(returncode=self.rc, stdout=self.stdout, stderr="")


def pio_build_cases():
    saved = ms.ROOT, ms.pio_run
    try:
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = fake = FakeBuild(root, map_text=ALPHA_MAP)
            got = ms.pio_build("pico_w_alpha")
            check("[g1] pio_build asks for the linker map", fake.calls == [("pico_w_alpha", True)], fake.calls)
            check("[g1] pio_build reads used, RAM and the fill",
                  got == {"flash": ALPHA_USED, "ram": 125680, "fill": 548}, got)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = FakeBuild(root, map_text=None)
            check("[g2] a build that left no map is refused", ms.pio_build("pico_w_alpha") is None)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            wrong, _ = mk_map(rodata_raw=REAL_TAIL, rodata_size=REAL_RODATA_SIZE, used=ALPHA_USED + 16)
            ms.pio_run = FakeBuild(root, map_text=wrong)
            check("[c6] a build whose map does not add up to `used` is refused", ms.pio_build("pico_w_alpha") is None)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = FakeBuild(root, stdout=ms.NO_ZOPFLI + "\n" + SIZES_OUT, map_text=ALPHA_MAP)
            check("[g2] a build without zopfli is refused", ms.pio_build("pico_w_alpha") is None)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = FakeBuild(root, returncode=1, map_text=ALPHA_MAP)
            check("[g2] a failed build is refused", ms.pio_build("pico_w_alpha") is None)
    finally:
        ms.ROOT, ms.pio_run = saved


def pio_run_cases():
    seen = {}

    def fake_run(cmd, **kw):
        seen.update(kw)
        return types.SimpleNamespace(returncode=0, stdout="", stderr="")

    saved_run, saved_flags = ms.subprocess.run, os.environ.get("PLATFORMIO_BUILD_FLAGS")
    try:
        ms.subprocess.run = fake_run
        os.environ["PLATFORMIO_BUILD_FLAGS"] = "-DX=1"
        ms.pio_run("pico_w_alpha", linker_map=True)
        flags = (seen.get("env") or {}).get("PLATFORMIO_BUILD_FLAGS", "")
        check("[g3] the map flag reaches the build, after the flags already set",
              flags == "-DX=1 " + ms.MAP_FLAG, flags)
        seen.clear()
        ms.pio_run("pico_w_alpha")
        check("[g3] without linker_map the build inherits the environment untouched", seen.get("env") is None)
        # SIMUT_SAVINGS_PIO names the pio to run, ahead of ~/.platformio/penv/bin/pio: on this host the penv broke under
        # python 3.14 on 2026-10-24 (No module named 'platformio') and the matrix of 2026-10-29 died on its first base.
        # Not "SIMUT_PIO": build.yml has a SIMUT_PIO_SOURCES that is something else (the review of 2026-10-29).
        cmds = []
        ms.subprocess.run = lambda cmd, **kw: (cmds.append(cmd), types.SimpleNamespace(returncode=0, stdout="", stderr=""))[1]
        saved_pio = os.environ.get("SIMUT_SAVINGS_PIO")
        try:
            os.environ["SIMUT_SAVINGS_PIO"] = sys.executable          # a file that exists
            ms.pio_run("pico_w_alpha")
            check("SIMUT_SAVINGS_PIO is the pio that runs", cmds and cmds[-1][0] == sys.executable, cmds[-1:])
            os.environ["SIMUT_SAVINGS_PIO"] = "/nonexistent/pio"
            try:
                ms.pio_run("pico_w_alpha")
                said = None
            except SystemExit as e:
                said = str(e)
            check("a SIMUT_SAVINGS_PIO that does not exist stops with its name, not a raw FileNotFoundError",
                  said is not None and "SIMUT_SAVINGS_PIO" in said and "/nonexistent/pio" in said, said)
            os.environ.pop("SIMUT_SAVINGS_PIO")
            ms.pio_run("pico_w_alpha")
            penv = os.path.expanduser("~/.platformio/penv/bin/pio")
            want = penv if os.path.exists(penv) else "pio"
            check("without SIMUT_SAVINGS_PIO: the penv's pio when it exists, else the PATH's", cmds[-1][0] == want, cmds[-1:])
        finally:
            if saved_pio is None:
                os.environ.pop("SIMUT_SAVINGS_PIO", None)
            else:
                os.environ["SIMUT_SAVINGS_PIO"] = saved_pio
    finally:
        ms.subprocess.run = saved_run
        if saved_flags is None:
            os.environ.pop("PLATFORMIO_BUILD_FLAGS", None)
        else:
            os.environ["PLATFORMIO_BUILD_FLAGS"] = saved_flags


def update_cases():
    """--update records the number --check compares: as measured, the tolerance is only ever applied on the check."""
    saved = ms.LEDGER
    with tempfile.TemporaryDirectory() as d:
        ms.LEDGER = os.path.join(d, "feature_savings.json")
        with open(ms.LEDGER, "w") as fh:
            json.dump({"mdns": {"base": "pico_w_release", "flash_saved": 16604, "ram_saved": 168}}, fh)
        results = {"web_metrics": {"base": "pico_w_release", "flash_saved": 4955, "ram_saved": 0},
                   "license_stub": {"base": "pico_w_test", "flash_saved": -2572, "ram_saved": 0},
                   "air": {"base": "pico_w_air", "flash_saved": 1, "ram_saved": 0},
                   "tel_mqtt": {"base": "pico_w_air", "error": True}}
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                ms.cmd_update(results)
            with open(ms.LEDGER) as fh:
                led = json.load(fh)

            def check_rc(flash):
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    return ms.cmd_check({"web_metrics": {"base": "pico_w_release", "flash_saved": flash,
                                                         "ram_saved": 0}}, ["web_metrics"])
            tol = getattr(ms, "FLASH_TOL", None)       # None on a tool that has no tolerance: a failed case, not a crash
            rt = None if tol is None else (check_rc(4955), check_rc(4955 - tol), check_rc(4955 - tol - 1))
        finally:
            ms.LEDGER = saved
    check("[h1] --update records the measured flash number, not minus the tolerance",
          led.get("web_metrics") == {"base": "pico_w_release", "flash_saved": 4955, "ram_saved": 0}, led)
    check("[h1] --update keeps the floors it did not measure and skips the inverted, coupled and failed ones",
          led.get("mdns", {}).get("flash_saved") == 16604
          and not {"license_stub", "air", "tel_mqtt"} & set(led), led)
    check("[h1] a floor --update wrote: the measured number and FLASH_TOL below it pass --check, one byte more fails",
          rt == (0, 0, 1), rt)


def matrix_cases():
    """--matrix builds with pio_build_full: the whole `used`, the .bin and its sha256, and no linker map."""
    saved = ms.ROOT, ms.pio_run
    blob = bytes(range(256)) * 20
    try:
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = fake = FakeBuild(root, map_text=ALPHA_MAP, bin_bytes=blob)
            got = ms.pio_build_full("pico_w_alpha")
    finally:
        ms.ROOT, ms.pio_run = saved
    want = {"used": ALPHA_USED, "bin": len(blob), "ram": 125680, "sha256": hashlib.sha256(blob).hexdigest()}
    check("[h2] pio_build_full (the --matrix side) returns the whole used, the .bin size and its sha256",
          got == want, got)
    check("[h2] pio_build_full does not ask for the linker map", fake.calls == [("pico_w_alpha", False)], fake.calls)


# ---- --matrix --only: the bases are rebuilt and compared with the file before a flip is built ------------------------
PARTIAL_BASE = {"used": 1000000, "bin": 1010000, "ram": 100000}     # what the scratch feature_costs.json says of pico_w_release
PARTIAL_FLIP = {"used": 990000, "bin": 1000000, "ram": 99000}       # the mdns flip, as the faked build returns it


class NoTempEnvs:
    """TempEnvs without the file: the environments are named and never written, because the builds are faked."""

    def __init__(self, blocks):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def partial_run(base_now):
    """cmd_matrix_partial( ["mdns"] ) on a scratch feature_costs.json that holds PARTIAL_BASE for pico_w_release, with the
    builds faked: the base builds as `base_now` and the flip as PARTIAL_FLIP. Returns (the exit message or None, the return
    code, the file before, the file after, the environments built). ROOT is a temp dir, so nothing real is written."""
    saved = ms.ROOT, ms.COSTS_PATH, ms.TempEnvs, ms.pio_build_full, ms._git
    built = []

    def fake_build(env, echo=False):
        built.append(env)
        base = env == "pico_w_release"
        return dict(base_now if base else PARTIAL_FLIP, sha256="base" if base else "flip")

    try:
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT, ms.COSTS_PATH = root, os.path.join(root, "feature_costs.json")
            ms.TempEnvs, ms.pio_build_full, ms._git = NoTempEnvs, fake_build, lambda *a: ""
            with open(ms.COSTS_PATH, "w") as fh:
                json.dump({"measured_at": {"date": "2026-10-30"},
                           "products": {"pico_w_release": {"base": dict(PARTIAL_BASE), "flips": {}}}}, fh)
            with open(ms.COSTS_PATH) as fh:
                before = fh.read()
            said = rc = None
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    rc = ms.cmd_matrix_partial(["mdns"])
            except SystemExit as e:
                said = str(e)
            with open(ms.COSTS_PATH) as fh:
                after = fh.read()
    finally:
        ms.ROOT, ms.COSTS_PATH, ms.TempEnvs, ms.pio_build_full, ms._git = saved
    return said, rc, before, after, built


def matrix_partial_cases():
    """`--matrix --only <key>` rebuilds each published base and compares it with the file BEFORE it builds a flip: the flips
    it records are the file's only while the bases are still the file's images. The comparison is `used`, `bin` and `ram`.
    S11 B2 (2026-10-30): a mutant that dropped `used` from it passed the whole suite, because no case ran the function. Each
    refusal below differs from the control in ONE field, so a mutant that drops that field fails exactly that case."""
    def control():
        said, rc, before, after, built = partial_run(PARTIAL_BASE)
        flips = json.loads(after)["products"]["pico_w_release"]["flips"]
        return (said is None and rc == 0 and built == ["pico_w_release", "_cost_pico_w_release_mdns"]
                and flips == {"mdns": PARTIAL_FLIP}), (said, rc, built, flips)
    case("[i0] --matrix --only: a base that matches the file lets the flip through, builds it and records it", control)

    for field, ident in (("used", "i1"), ("bin", "i2"), ("ram", "i3")):
        def refused_alone(field=field):
            now = dict(PARTIAL_BASE, **{field: PARTIAL_BASE[field] + 8})
            said, rc, before, after, built = partial_run(now)
            return (said is not None and "pico_w_release" in said and f"{field}={now[field]}" in said and "--matrix" in said
                    and after == before and built == ["pico_w_release"]), (said, rc, after == before, built)
        case(f"[{ident}] --matrix --only: a base whose `{field}` alone changed is refused with the product, the new number and what "
             "to run; the file is not rewritten and no flip is built", refused_alone)


def measure_cases():
    """measure( ) with the builds faked, and the temporary environments written to a scratch file."""
    saved = ms.PROFILES_INI, ms.pio_build
    with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as fh:
        fh.write("; scratch\n")
    try:
        ms.PROFILES_INI = fh.name

        def saving(feature, sizes):
            ms.pio_build = lambda env: dict(sizes[env])
            return ms.measure([feature])[feature]

        bt = {"pico_w_alpha": {"flash": 976236, "ram": 125680, "fill": 548},
              "_sav_bluetooth_off": {"flash": 838880, "ram": 106332, "fill": 0}}
        r = {}
        case("[g4] the saving takes each side's fill off: (976236-548) - (838880-0)",
             lambda: (r.update(saving("bluetooth", bt)) or r["flash_saved"] == 976236 - 548 - 838880, r))
        case("[e2] bluetooth on the Alpha saves 136808 B, the number the nightly printed on 2026-10-27..29",
             lambda: (r["flash_saved"] == 136808, r))
        case("[g4] the raw numbers and both fills are kept for the table",
             lambda: ((r["flash_base"], r["flash_off"], r["fill_base"], r["fill_off"]) == (976236, 838880, 548, 0), r))
        with open(fh.name) as f2:
            check("[g4] the temporary environments are taken out again", f2.read() == "; scratch\n")

        # [a2] the same content with other fills: the same saving
        one = {"pico_w_release": {"flash": 12012, "ram": 100, "fill": 12},
               "_sav_web_metrics_off": {"flash": 4004, "ram": 100, "fill": 4}}
        two = {"pico_w_release": {"flash": 12000, "ram": 100, "fill": 0},
               "_sav_web_metrics_off": {"flash": 4012, "ram": 100, "fill": 12}}
        case("[a2] the same content with other fills in the base and the variant: the same saving, 8000",
             lambda: ((lambda a, b: (a == b == 8000, (a, b)))(saving("web_metrics", one)["flash_saved"],
                                                              saving("web_metrics", two)["flash_saved"])))

        # [e3] the Air's tel_tls on 2026-10-22: two trees that remove the same symbols, bank fills 4096 B apart
        t1 = {"pico_w_air": {"flash": 1000000, "ram": 5, "fill": 2300},
              "_sav_tel_tls_off": {"flash": 940000, "ram": 5, "fill": 2540}}
        t2 = {"pico_w_air": {"flash": 1004096, "ram": 5, "fill": 6396},
              "_sav_tel_tls_off": {"flash": 944096, "ram": 5, "fill": 6636}}
        case("[e3] the Air's 4096 B step in both bank fills does not move the saving",
             lambda: ((lambda a, b: (a == b == 60240, (a, b)))(saving("tel_tls", t1)["flash_saved"],
                                                              saving("tel_tls", t2)["flash_saved"])))
    finally:
        ms.PROFILES_INI, ms.pio_build = saved
        os.unlink(fh.name)


def flips_cases():
    """The cost matrix's flips for the look of the touch display (2026-10-29): `ui` is a choice, like the network, so a product's flips
    have one `ui=<id>` per [ui.<id>] of the manifest that is not its own, built where the rules allow it and recorded with the rule
    where they do not. Read from the real manifest: these are the four published products the configurator prices."""
    gf = ms.gf
    M = gf.load_manifest()
    check("a choice's key carries its value: ui=angulo", ms.flip_key("ui", "angulo") == "ui=angulo", ms.flip_key("ui", "angulo"))
    check("the network's key is unchanged: net=enc28j60", ms.flip_key("net", "enc28j60") == "net=enc28j60")
    check("a switch's key is its name", ms.flip_key("buzzer", True) == "buzzer")
    check("the build env of a ui flip has no '=' (PlatformIO refuses it)", "=" not in ms.cost_env("pico2_w_release", "ui=original"))
    p2 = gf.resolve_profile("pico2_w_release", M["profiles"])
    flips = dict(ms.flips_of(p2, M))
    # S10c C3 (2026-10-30): the Pico 2 W's base is the Ângulo image, so its look flip goes to the original (it went to the Ângulo until then).
    check("pico2_w_release flips to the original", "ui=original" in flips, sorted(k for k in flips if "=" in k))
    check("the flipped profile asks for the original and keeps the rest",
          "ui=original" in flips and flips["ui=original"].get("ui") == "original"
          and {k: v for k, v in flips["ui=original"].items() if k != "ui"} == {k: v for k, v in p2.items() if k != "ui"})
    check("no flip to the product's own look", "ui=" + p2.get("ui", gf.DEFAULT_UI) not in flips)
    only = ms.flips_of(p2, M, ["ui"])
    check("--only ui gives the ui flips alone", [k for k, _ in only] == ["ui=original"], [k for k, _ in only])
    plans = {}
    for prod in ("pico_w_release", "pico_w_alpha", "pico_w_air", "pico2_w_release"):
        prof = gf.resolve_profile(prod, M["profiles"])
        blocks, plan = [], []
        for key, flipped in ms.flips_of(prof, M, ["ui"]):
            ms.plan_flip(blocks, plan, prod, key, flipped, M)
        plans[prod] = (blocks, plan)
    b, pl = plans["pico2_w_release"]
    check("the original on the Pico 2 W is built: one env, no rule", len(b) == 1 and pl[0][2] and pl[0][3] is None, pl)
    check("... and its env names no RAM budget: an original image has no Ângulo objects for the gate to hold", len(b) == 1 and "custom_ui_ram_budget_env" not in b[0], b[0][-160:] if b else b)
    # The Ângulo's static-RAM gate (tools/check_ui_ram.py) holds every environment to an entry in tools/ui_ram_budget.json, and a
    # temporary one has none: the matrix of 2026-10-29 lost this flip at the end of its link ("[ui-ram] FATAL: no static-RAM budget
    # for _cost_pico2_w_release_ui_angulo"). The env names the real one whose entry holds it, as build_custom.py does for _custom.
    # Since S10c C3 the Ângulo flip is one from the original image (pico2_w_original), and the real env on that hardware is pico2_w_release.
    po = gf.resolve_profile("pico2_w_original", M["profiles"])
    bo, plo = [], []
    for key, flipped in ms.flips_of(po, M, ["ui"]):
        ms.plan_flip(bo, plo, "pico2_w_original", key, flipped, M)
    check("the Ângulo flip of pico2_w_original is built: one env, no rule", len(bo) == 1 and plo[0][1] == "ui=angulo" and plo[0][2] and plo[0][3] is None, plo)
    check("the Ângulo's env names the budget of the real Ângulo env on that hardware (pico2_w_release)",
          len(bo) == 1 and "\ncustom_ui_ram_budget_env = pico2_w_release\n" in "\n" + bo[0], bo[0][-160:] if bo else bo)
    # No real Ângulo env holds this hardware's budget: the flip is not built and says why (the review of 2026-10-29 found
    # this branch untested). build_custom.ui_ram_env stubbed to answer "none".
    import build_custom as bc
    saved_ure = bc.ui_ram_env
    try:
        bc.ui_ram_env = lambda prof, M_, budgets=None: (None, "no budget here")
        b2, pl2 = [], []
        for key, flipped in ms.flips_of(po, M, ["ui"]):
            ms.plan_flip(b2, pl2, "pico2_w_original", key, flipped, M)
        check("with no real Ângulo env for the hardware the flip is unsupported, with the reason, and not built",
              b2 == [] and pl2 == [("pico2_w_original", "ui=angulo", None, {"unsupported": "no budget here"})], (b2, pl2))
    finally:
        bc.ui_ram_env = saved_ure
    # The checks (S10c C3, 2026-10-30): the Pico 2 W's own look is the Ângulo, so no check sets it (a `set` holds changes, and `ui=angulo` is no flip of pico2_w_release: the matrix run of that day
    # ended in print_checks( ) with KeyError on the check that did); one check sets the original, the change that exists, and its env names no RAM budget (the original holds no Ângulo objects).
    eff = lambda p, ch: ch.get("ui", gf.resolve_profile(p, M["profiles"]).get("ui", gf.DEFAULT_UI))
    own = [(p, ch) for p, ch in ms.CHECKS if ch.get("ui") == gf.resolve_profile(p, M["profiles"]).get("ui", gf.DEFAULT_UI)]
    check("no check sets the product's own look (a set holds changes only)", own == [], own)
    orig = [(i, c) for i, c in enumerate(ms.CHECKS) if c[1].get("ui") == "original"]
    check("one check sets the original, on the Pico 2 W", len(orig) == 1 and orig[0][1][0] == "pico2_w_release", orig)
    if orig:
        prof = gf.resolve_profile(orig[0][1][0], M["profiles"])
        prof.update(orig[0][1][1])
        text, why = ms.env_text("_check_x", prof, M)
        check("the original check's env names no RAM budget", why is None and "custom_ui_ram_budget_env" not in text, why)
    first_two = [c for c in ms.CHECKS if c[0] == "pico2_w_release" and "ui" not in c[1]]
    prof = gf.resolve_profile(first_two[0][0], M["profiles"])
    prof.update(first_two[0][1])
    text, why = ms.env_text("_check_x", prof, M)
    check("a Pico 2 W check on the product's own look (the Ângulo) names the budget of pico2_w_release",
          why is None and "\ncustom_ui_ram_budget_env = pico2_w_release\n" in "\n" + text, why)
    # measure_checks( ) itself builds its environments through env_text( ): the TempEnvs and the builds stubbed, exactly one of
    # the CHECKS environments names an Ângulo budget, and it is the one whose set carries the Ângulo (the review of 1414638d:
    # a revert of measure_checks( ) to the bare emit_env( ), or an env_text( ) that looks the budget up for every profile, both
    # passed the suite then; either kills `--checks` at the end of a link, hours later).
    seen_blocks = []

    class FakeTempEnvs:
        def __init__(self, blocks):
            seen_blocks.extend(blocks)

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            return False
    saved_te, saved_pbf = ms.TempEnvs, ms.pio_build_full
    try:
        ms.TempEnvs = FakeTempEnvs
        ms.pio_build_full = lambda env, echo=False: {"used": 1, "bin": 1, "ram": 1, "sha256": "x"}
        out = ms.measure_checks()
    finally:
        ms.TempEnvs, ms.pio_build_full = saved_te, saved_pbf
    with_budget = [b.splitlines()[0] for b in seen_blocks if "custom_ui_ram_budget_env = " in b]
    want = [f"[env:_check_{i}]" for i, c in enumerate(ms.CHECKS) if eff(c[0], c[1]) == "angulo"]
    check("measure_checks( ): one environment per check, and only the Ângulo images' name a RAM budget (two, the Pico 2 W's own look since S10c C3)",
          len(seen_blocks) == len(ms.CHECKS) and len(out) == len(ms.CHECKS) and with_budget == want and len(want) == 2,
          (len(seen_blocks), with_budget, want))
    # print_checks( ) names a choice by its value: "ui=angulo", not "ui=on" (a switch prints on/off).
    import io, contextlib
    doc = {"products": {"pico2_w_release": {"base": {"used": 100, "bin": 100, "ram": 10},
                                            "flips": {"ui=angulo": {"used": 130, "bin": 130, "ram": 12},
                                                      "mdns": {"used": 90, "bin": 90, "ram": 9}}}},
           "checks": [{"base": "pico2_w_release", "set": {"ui": "angulo", "mdns": False}, "used": 120, "bin": 120, "ram": 11}]}
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ms.print_checks(doc)
    printed = buf.getvalue()
    check("print_checks( ) prints a choice with its value and a switch on/off",
          "ui=angulo, mdns=off" in printed and "ui=on" not in printed, printed.strip())
    for prod, rule in (("pico_w_release", "angulo-cyw43-needs-rp2350"), ("pico_w_alpha", "angulo-needs-tft"),
                       ("pico_w_air", "angulo-needs-tft")):
        b, pl = plans[prod]
        check(f"the Ângulo on {prod} is not built and names its rule ({rule})",
              not b and len(pl) == 1 and pl[0][2] is None and pl[0][3] == rule, pl)


def table_cases():
    res = {"bluetooth": {"base": "pico_w_alpha", "flash_saved": 136808, "ram_saved": 19348,
                         "flash_base": 976236, "flash_off": 838880, "fill_base": 548, "fill_off": 0}}
    out = io.StringIO()

    def table():
        with contextlib.redirect_stdout(out):
            ms.print_table(res)
        return "548" in out.getvalue() and "enchimento" in out.getvalue(), out.getvalue()
    case("[g5] the table says the fill was taken off, with both sides' bytes", table)


def main():
    reading_cases()
    refusal_cases()
    bank_cases()
    fill_not_in_number_cases()
    gate_cases()
    pio_build_cases()
    pio_run_cases()
    update_cases()
    matrix_cases()
    matrix_partial_cases()
    measure_cases()
    table_cases()
    flips_cases()
    print(f"\n{len(RAN) - len(FAILS)}/{len(RAN)} ok")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
