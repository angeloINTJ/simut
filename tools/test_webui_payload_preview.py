#!/usr/bin/env python3
"""The Live Preview of the telemetry page, held to the firmware.

The two previews build in the browser the body the device sends: the
measurement line (TelemetryManager::buildPayload, with formatLineJsonBuf,
formatLineCustomBuf and toCsvLine) and the alarm line (alarmFormatLine and
alarmFormatCsvLine, in src/AlarmPayload.h). They build it from this device:
its name, MAC and board serial, its clock and history interval, the readings
of now, the alarm limits, and the account that is logged in. A preview that
says one thing while the device sends another is worse than none, and it
happened twice: on 2026-10-03 the alarm preview did not know the v23 and v24
tokens, so the firmware's own default template came out with
`"maint":{maint}` in it and the organized view called it invalid JSON; and
the CSV previews had one column per active sensor where the device sends 34,
and 4 where it sends 8.

The test cuts the preview's functions out of TEL_PAGE, ending each one with
the build's own JS scanner (a '{' in a string is the walkers' whole
subject), and runs them under node, as written and as the minifier leaves
them, against:

- the firmware's own vectors, where they exist: test_alarm_queue's alarm
  lines and CSV rows, each checked to still be in that file, and every token
  alarmFormatLine matches;
- vectors derived from the C++, where none exist: formatLineJsonBuf,
  formatLineCustomBuf, toCsvLine and the CSV header of buildPayload, each
  group naming the function it follows;
- a device described by the four API answers the page reads (/api/config,
  /api/status, /api/alarms, /api/perms), whose name, MAC, readings, limits
  and account must reach the records; and every alarm code the firmware has,
  read off AlarmPayload.h, must appear in the alarm preview once, with
  exactly the fields that code carries.

Run: python3 tools/test_webui_payload_preview.py
Exit status is 0 on pass, 1 on failure.
"""

import functools
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WEBUI = REPO / "WebUI.h"
GEN = REPO / "tools" / "build_webui_gz.py"
ALARM_H = REPO / "src" / "AlarmPayload.h"
ALARM_TEST = REPO / "test" / "test_alarm_queue" / "test_main.cpp"
STORAGE = REPO / "src" / "StorageManager.cpp"
FUNCS = ("_resolveCustom", "_sensorTokenResolver", "_previewCustomLine", "_alarmTokenResolver",
         "_alarmCustomLine", "_previewGlobal", "_sensorJsonLine", "_sensorCsv", "_alarmCsvLine",
         "_alarmCsv", "_pvFrom", "_sensorBatch", "_alarmBatch")

# ── The alarm line: the firmware's own vectors ──────────────────────────────
#
# From test/test_alarm_queue/test_main.cpp. A template key names the template
# the vector was made with: "t21" is fillDemoCfg's, "v24" is V24_TEMPLATE (the
# default), the others are the literal templates of their tests. Each record
# is written the way the preview's batch writes one: the fields already
# formatted as AlarmPayload.h formats them, the codes and the user bare.
ALARM_TEMPLATES_LITERAL = {
    "ch": "{CH};{SLOT};{HWID};{VAL};{ERR}",
    "upper": '{"VAL":{VAL},"ERR":{ERR}}',
    "literal": "{[notatoken]:1}",
}
ALARM_VECTORS = [
    ["t21", {"ts": 1756250000, "id": "tSENSOR1", "val": "25.30", "alarm": "alarm", "seq": 1},
     '{"ts":1756250000,"id":"tSENSOR1","val":25.30,"alarm":"alarm","seq":1}'],
    ["t21", {"ts": 1756250100, "id": "tSENSOR1", "err": "err", "seq": 2},
     '{"ts":1756250100,"id":"tSENSOR1","err":"err","seq":2}'],
    ["t21", {"ts": 1756250500, "id": "tSENSOR1", "alarm": "alarm_sil", "seq": 6},
     '{"ts":1756250500,"id":"tSENSOR1","alarm":"alarm_sil","seq":6}'],
    ["v24", {"ts": 1700000000, "id": "tS2", "maint": "maint_on", "until": 1700007200, "user": "joao", "seq": 9},
     '{"ts":1700000000,"id":"tS2","maint":"maint_on","until":1700007200,"user":"joao","seq":9}'],
    ["v24", {"ts": 1700000000, "id": "tS2", "maint": "maint_off", "seq": 10},
     '{"ts":1700000000,"id":"tS2","maint":"maint_off","seq":10}'],
    ["v24", {"ts": 1700000100, "id": "tS2", "alarm": "alarm_lim", "lo": "-5.00", "hi": "30.50",
             "user": "admin", "seq": 11},
     '{"ts":1700000100,"id":"tS2","alarm":"alarm_lim","lo":-5.00,"hi":30.50,"user":"admin","seq":11}'],
    ["v24", {"ts": 1700000100, "id": "uS2", "alarm": "alarm_lim", "lo": "30.0", "hi": "80.0",
             "user": "admin", "seq": 11},
     '{"ts":1700000100,"id":"uS2","alarm":"alarm_lim","lo":30.0,"hi":80.0,"user":"admin","seq":11}'],
    ["v24", {"ts": 1700000100, "id": "tS2", "alarm": "alarm_on", "user": "admin", "seq": 12},
     '{"ts":1700000100,"id":"tS2","alarm":"alarm_on","user":"admin","seq":12}'],
    # Outside the compound form a token with no value is empty, not null as
    # in the measurement line.
    ["ch", {"ch": "u", "slot": 0, "hwid": "SENSOR1", "val": "101.3", "alarm": "alarm", "seq": 7},
     "u;0;SENSOR1;101.3;"],
    ["ch", {"ch": "u", "slot": 0, "hwid": "SENSOR1", "err": "err", "seq": 7},
     'u;0;SENSOR1;;"err"'],
    ["upper", {"val": "25.30", "alarm": "alarm"}, '{"VAL":25.30}'],
    ["upper", {"err": "err"}, '{"ERR":"err"}'],
    ["literal", {"val": "25.30", "alarm": "alarm"}, "{[notatoken]:1}"],
]
ALARM_CSV_VECTORS = [
    [{"seq": 4, "ts": 1756250300, "id": "tSENSOR1", "alarm": "alarm", "val": "25.30"},
     "4;1756250300;tSENSOR1;25.30;;;;"],
    [{"seq": 5, "ts": 1756250400, "id": "tSENSOR1", "err": "err"}, "5;1756250400;tSENSOR1;err;;;;"],
    [{"seq": 6, "ts": 1756250500, "id": "tSENSOR1", "alarm": "alarm_sil"},
     "6;1756250500;tSENSOR1;alarm_sil;;;;"],
    [{"seq": 7, "ts": 1756250600, "id": "tSENSOR1", "err": "err_sil"}, "7;1756250600;tSENSOR1;err_sil;;;;"],
    [{"seq": 8, "ts": 1756250700, "id": "tSENSOR1", "alarm": "alarm_off"},
     "8;1756250700;tSENSOR1;alarm_off;;;;"],
    [{"seq": 9, "ts": 1756250800, "id": "tSENSOR1", "err": "err_off"}, "9;1756250800;tSENSOR1;err_off;;;;"],
    [{"seq": 14, "ts": 1700000300, "id": "tS2", "alarm": "alarm_lim", "user": "joao", "lo": "-5.00",
      "hi": "30.50"}, "14;1700000300;tS2;alarm_lim;joao;-5.00;30.50;"],
    [{"seq": 15, "ts": 1700000400, "id": "tS2", "maint": "maint_on", "user": "joao", "until": 1700004000},
     "15;1700000400;tS2;maint_on;joao;;;1700004000"],
    [{"seq": 4, "ts": 1756250300, "id": "tS2", "alarm": "alarm", "val": "25.30"}, "4;1756250300;tS2;25.30;;;;"],
]
# Derived from alarmFormatCsvLine, not in the native suite: the code decides
# the value column (alarmCodeHasValue), so a marker that happens to hold a
# value still shows its code.
ALARM_CSV_DERIVED = [
    [{"seq": 6, "ts": 1756250500, "id": "tSENSOR1", "alarm": "alarm_sil", "val": "25.30"},
     "6;1756250500;tSENSOR1;alarm_sil;;;;"],
]

# Which optional fields each code carries: the table "Os códigos" of chapter
# 22 of the manual, the contract the collector is written against. alarm_sil
# and err_sil carry no user because the panel's Silence asks for no PIN; a
# maint_off carries one when someone closed the window, and the preview shows
# that case (a deadline closes it with none).
CARRIES = {
    "alarm": ["val"], "alarm_sil": [], "alarm_off": ["user"], "alarm_on": ["user"],
    "alarm_lim": ["lo", "hi", "user"], "err": [], "err_sil": [], "err_off": ["user"],
    "maint_on": ["until", "user"], "maint_off": ["user"],
}

# ── The measurement line: vectors derived from the C++ ──────────────────────
#
# No native suite compiles TelemetryManager, so these follow the code: a
# record is what collectDay() builds (temperature and humidity per slot, ONE
# pressure), and each formatter is read from TelemetryManager.cpp and
# SystemDefs_Records.h. A slot is [hwid, active, temperature, humidity,
# has pressure]; slots not listed are inactive and empty.
SENSOR_RECORDS = {
    "two": [1700000000, "1013.2", [["STM0009", True, "21.50", None, False],
                                   ["BME28001", True, "22.10", "55.0", True]]],
    "fail0": [1700000000, "1013.2", [["STM0009", True, None, None, False],
                                     ["BME28001", True, "22.10", "55.0", True]]],
    "nohwid": [1700000060, None, [["", True, "20.00", "50.5", False], None, None,
                                  ["", True, "19.75", None, False]]],
    "pp": [1700000120, "1000.0", [["", True, "20.00", None, True]]],
    "humonly": [1700000180, None, [["DHT01", True, None, "48.0", False]]],
    "inactive": [1700000240, None, [["OLD01", False, "30.00", "40.0", False],
                                    ["STM0009", True, "21.50", None, False]]],
}
SERIAL = "E6610A2B3C4D5E6F"
# formatLineJsonBuf: every active slot's temperature and humidity that is not
# NaN, keyed by hwId (or the slot number); then the one pressure, keyed by the
# FIRST active slot that has an hwId and reports pressure, or by "p" when none
# does. A failed sensor's key is left out, not sent as null.
SENSOR_JSON = {
    "two": '{"ts":1700000000,"tSTM0009":21.50,"tBME28001":22.10,"uBME28001":55.0,"pBME28001":1013.2}',
    "fail0": '{"ts":1700000000,"tBME28001":22.10,"uBME28001":55.0,"pBME28001":1013.2}',
    "nohwid": '{"ts":1700000060,"t0":20.00,"u0":50.5,"t3":19.75}',
    "pp": '{"ts":1700000120,"t0":20.00,"pp":1000.0}',
    "humonly": '{"ts":1700000180,"uDHT01":48.0}',
    "inactive": '{"ts":1700000240,"tSTM0009":21.50}',
}
# formatLineCustomBuf: {TS}, {DHT_ID} (the board serial), {t0}..{t15},
# {u0}..{u15} and {p0}..{p15}. Two digits are 10..15 for t and u, and any
# pair for p ({p09} is slot 9, {t09} is text). A bare token with no value is
# null; the compound forms "<k>_ID":{<k>} and "<k>":{<k>} drop the key.
SENSOR_CUSTOM = [
    ['{"ts":{TS},"t0_ID":{t0},"u1_ID":{u1},"p1_ID":{p1}}', "two",
     '{"ts":1700000000,"tSTM0009":21.50,"uBME28001":55.0,"pBME28001":1013.2}'],
    ['{"ts":{TS},"t0_ID":{t0},"u1_ID":{u1},"p1_ID":{p1}}', "fail0",
     '{"ts":1700000000,"uBME28001":55.0,"pBME28001":1013.2}'],
    ["{TS};{t0};{u0};{p0};{DHT_ID}", "two", "1700000000;21.50;null;null;" + SERIAL],
    ['{"t0":{t0},"t1":{t1}}', "fail0", '{"t1":22.10}'],
    ['{"p1":{p1},"p0":{p0}}', "two", '{"p1":1013.2}'],
    ["{p09}|{t09}|{t16}|{u10}|{t15}|{p25}", "two", "null|{t09}|{t16}|null|null|{p25}"],
    ['{"t0":{t0}}', "inactive", "{}"],
]
# The custom envelope: {DEV} is the device name, {MAC} the Wi-Fi MAC, {DATA}
# the lines; any other '{' is text.
ENVELOPE = [
    ['{"dev":"{DEV}","mac":"{MAC}","data":[{DATA}]}', "lab-tft", "28:CD:C1:0A:1B:2C", '{"ts":1}',
     '{"dev":"lab-tft","mac":"28:CD:C1:0A:1B:2C","data":[{"ts":1}]}'],
    ["{X}{DEV}{DATA}{MAC}", "n", "m", "d", "{X}ndm"],
]

# ── A device, as the four API answers describe it ───────────────────────────
#
# Shaped like the firmware writes them (handleApiConfig, handleApiStatus,
# handleApiAlarms, handleApiPerms); the numbers arrive as JSON numbers, so
# 21.50 reads back as 21.5 and the preview has to put the decimals back.
DEV_CONFIG = {
    "name": "lab-tft", "now_epoch": 1790000000, "h_int": 5, "serial": SERIAL,
    "sensors": [{"hwid": "STM0009", "active": True, "hum": False, "press": False},
                {"hwid": "BME28001", "active": True, "hum": True, "press": True},
                {"hwid": "OLD01", "active": False, "hum": False, "press": False}]
    + [{"hwid": "", "active": False, "hum": False, "press": False}] * 13,
}
DEV_STATUS = {
    "sys": {"name": "lab-tft", "mac": "28:CD:C1:0A:1B:2C", "uid": SERIAL, "time": 1790000123, "hi": 5},
    "sensors": [{"slot": 0, "id": "STM0009", "val": 21.5},
                {"slot": 1, "id": "BME28001", "val": 22.1, "hum": 55, "press": 1013.3}],
}
DEV_ALARMS = {
    "sensors": [{"idx": 0, "type": "DS18B20", "lim": {"temp": [2.0, 8.0]}},
                {"idx": 1, "type": "BME280", "lim": {"temp": [0.0, 40.0], "hum": [30.0, 70.0],
                                                   "press": [0.0, 1638.3]}}],
}
DEV_USER = "joao"

HARNESS = r"""
const D = __DATA__;
const out = [];
const eq = (label, got, want) => {
  const g = JSON.stringify(got), w = JSON.stringify(want);
  if (g !== w) out.push(label + '\n      want ' + w + '\n      got  ' + g);
};
const parses = (label, s) => { try { JSON.parse(s); } catch (e) { out.push(label + ' is not JSON: ' + s); } };
const rec = ([ts, press, list]) => ({ ts, serial: D.serial, press, slots: Array.from({ length: 16 }, (_, i) => {
  const s = list[i] || ['', false, null, null, false];
  return { hwid: s[0], active: s[1], val: s[2], hum: s[3], hasPress: s[4] };
}) });

/* the alarm walker */
for (const t of D.tokens) {
  const r = _alarmTokenResolver({}, t, 0);
  if (!r || r.tc !== t.length) out.push('the firmware resolves ' + t + ', the preview leaves it in the text');
}
for (const [k, r, want] of D.alarm) eq('alarm line on ' + k + ' ' + JSON.stringify(r), _alarmCustomLine(D.tpl[k], r), want);
for (const [r, want] of D.alarmCsv.concat(D.alarmCsvDerived)) eq('alarm CSV ' + JSON.stringify(r), _alarmCsvLine(r), want);
eq('alarm CSV header', _alarmCsv([D.alarmCsv[0][0]]).split('\n')[0], 'seq;ts;id;v;user;lo;hi;until');

/* the measurement line */
for (const k in D.json) eq('JSON line ' + k, _sensorJsonLine(rec(D.rec[k])), D.json[k]);
for (const [t, k, want] of D.custom) eq('custom line ' + t + ' on ' + k, _previewCustomLine(t, rec(D.rec[k])), want);
for (const k of ['two', 'inactive']) eq('CSV of ' + k, _sensorCsv([rec(D.rec[k]), rec(D.rec.fail0)]), D.csv[k]);
for (const [g, dev, mac, data, want] of D.env) eq('envelope ' + g, _previewGlobal(g, dev, mac, data), want);

/* a device: the four answers reach the records */
const dv = _pvFrom(D.dev.config, D.dev.status, D.dev.alarms, D.dev.user);
eq('device name, MAC, serial, clock, interval, account', [dv.name, dv.mac, dv.uid, dv.time, dv.every, dv.user, dv.live],
   ['lab-tft', '28:CD:C1:0A:1B:2C', D.serial, 1790000123, 300, 'joao', true]);
eq('readings of now, with their decimals', dv.s.slice(0, 3).map(s => [s.hwid, s.active, s.t, s.u, s.p]),
   [['STM0009', true, '21.50', null, null], ['BME28001', true, '22.10', '55.0', '1013.3'], ['OLD01', false, null, null, null]]);
eq('limits', [dv.s[0].lim.t, dv.s[1].lim.u, dv.s[1].lim.p], [[2, 8], [30, 70], [0, 1638.3]]);

const sb = _sensorBatch(dv);
eq('measurement batch: two records, one history interval apart', sb.map(r => r.ts), [1790000123 - 300, 1790000123]);
eq('measurement batch: the first shows the first active sensor in failure', _sensorJsonLine(sb[0]),
   '{"ts":1789999823,"tBME28001":22.10,"uBME28001":55.0,"pBME28001":1013.3}');
eq('measurement batch: the second is the readings of now', _sensorJsonLine(sb[1]),
   '{"ts":1790000123,"tSTM0009":21.50,"tBME28001":22.10,"uBME28001":55.0,"pBME28001":1013.3}');
eq('measurement batch: the board serial', sb[1].serial, D.serial);
const sbody = _previewGlobal(D.def.glob, dv.name, dv.mac, sb.map(r => _previewCustomLine(D.def.line, r)).join(D.def.sep));
eq('measurement batch under the default templates', sbody,
   '{"dev":"lab-tft","mac":"28:CD:C1:0A:1B:2C","data":[{"ts":1789999823},{"ts":1790000123,"tSTM0009":21.50}]}');
parses('the measurement JSON body', '[' + sb.map(_sensorJsonLine).join(',') + ']');

const ab = _alarmBatch(dv);
eq('alarm batch: one record of every code the firmware has, once', ab.map(r => r.alarm || r.err || r.maint).sort(), D.codes.slice().sort());
eq('alarm batch: seq from 1, a minute apart, ending now', ab.map(r => [r.seq, r.ts]), ab.map((r, k) => [k + 1, 1790000123 - (ab.length - 1 - k) * 60]));
for (const r of ab) {
  const code = r.alarm || r.err || r.maint;
  eq('alarm batch: the fields ' + code + ' carries', ['val', 'lo', 'hi', 'until', 'user'].filter(f => r[f] != null), D.carries[code] || ['(code unknown to the test)']);
}
const byCode = c => ab.find(r => (r.alarm || r.err || r.maint) == c) || {};
eq('alarm: the reading of now, on the first active sensor', [byCode('alarm').id, byCode('alarm').val], ['tSTM0009', '21.50']);
eq('alarm_lim: the humidity limits of the sensor that has humidity', [byCode('alarm_lim').id, byCode('alarm_lim').lo, byCode('alarm_lim').hi], ['uBME28001', '30.0', '70.0']);
eq('maint_on: two hours to the end of the window', byCode('maint_on').until - byCode('maint_on').ts, 7200);
eq('the account of the session', byCode('err_off').user, 'joao');
const abody = '[' + ab.map(r => _alarmCustomLine(D.def.aline, r)).join(',') + ']';
parses('the alarm JSON body under the default template', abody);
eq('the alarm body carries every code', D.codes.filter(c => abody.indexOf('"' + c + '"') < 0), []);
eq('the alarm CSV has a row per record', _alarmCsv(ab).split('\n').length, ab.length + 2);

/* two pressure sensors: the record keeps the last one's reading, because
   takeRecord( ) overwrites and buildH5Schema( ) lays the file out slot by
   slot; the JSON key is still the first one's (formatLineJsonBuf). The
   firmware's own mismatch, shown as the device sends it. */
const c2 = JSON.parse(JSON.stringify(D.dev.config)), s2 = JSON.parse(JSON.stringify(D.dev.status));
c2.sensors[2] = { hwid: 'BMP02', active: true, hum: false, press: true };
s2.sensors.push({ slot: 2, id: 'BMP02', val: 23, press: 1000.1 });
eq('two pressure sensors', _sensorJsonLine(_sensorBatch(_pvFrom(c2, s2, null, ''))[1]),
   '{"ts":1790000123,"tSTM0009":21.50,"tBME28001":22.10,"uBME28001":55.0,"tBMP02":23.00,"pBME28001":1000.1}');

/* without /api/status the preview still draws, from the configuration */
const dn = _pvFrom(D.dev.config, null, null, '');
eq('without the status: not live, still the configured sensors', [dn.live, dn.name, dn.s[0].hwid, dn.s[1].hwid, dn.s[0].t != null], [false, 'lab-tft', 'STM0009', 'BME28001', true]);
eq('without the alarms: the factory limits', [dn.s[0].lim.t, dn.s[1].lim.u], [[0, 40], [20, 80]]);
parses('the alarm body without the status', '[' + _alarmBatch(dn).map(r => _alarmCustomLine(D.def.aline, r)).join(',') + ']');
const de = _pvFrom({ name: 'x', sensors: [] }, { error: 'Forbidden' }, null, '');
eq('a refused status is not live', de.live, false);
eq('no sensor at all: records still build', [_sensorBatch(de).length, _alarmBatch(de).length], [2, D.codes.length]);
console.log(JSON.stringify(out));
"""


@functools.lru_cache(maxsize=None)
def generator() -> dict:
    """build_webui_gz.py's functions, loaded without running its generate()."""
    ns = {"__name__": "build_webui_gz"}
    exec(compile(GEN.read_text(encoding="utf-8").replace("\ngenerate()\n", "\n"),
                 str(GEN), "exec"), ns)
    return ns


def block(src: str, name: str) -> str:
    """One page or asset of WebUI.h: from its declaration to the end of its raw string."""
    a = src.find(f"static const char {name}[] PROGMEM")
    if a < 0:
        raise SystemExit(f"FAIL: {name} is not in WebUI.h")
    return src[a:src.find(')raw";', a)]


def js_function(page: str, name: str) -> str:
    """One function of a page, ended by the build's own JS scanner: a brace in
    a string does not end it."""
    a = page.find(f"function {name}(")
    if a < 0:
        raise SystemExit(f"FAIL: TEL_PAGE has no {name}. If the preview moved, move this test with it.")
    depth, pos = 0, a
    for kind, text in generator()["_js_tokens"](page[a:]):
        if kind == "code":
            for i, ch in enumerate(text):
                depth += (ch == "{") - (ch == "}")
                if ch == "}" and depth == 0:
                    return page[a:pos + i + 1]
        pos += len(text)
    raise SystemExit(f"FAIL: {name} in TEL_PAGE has no end")


def c_string(src: str, anchor: str, where: str) -> str:
    """The C string literal right after `anchor`, its adjacent pieces joined."""
    a = src.find(anchor)
    m = re.match(r'\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', src[a + len(anchor):]) if a >= 0 else None
    if not m:
        raise SystemExit(f"FAIL: no string after {anchor!r} in {where}; this test reads a template there")
    return re.sub(r'\\(.)', r'\1', "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))))


def csv_expected(name: str) -> str:
    """The CSV body of [record `name`, record fail0], written from the C++:
    the header of buildPayload names all 16 slots, with the hwId only for an
    active slot that has one, then all 16 humidities, then press; toCsvLine
    writes epoch;t0..t15;h0..h15;press, an empty field for NaN."""
    def slots(n):
        lst = SENSOR_RECORDS[n][2]
        return [(lst[i] if i < len(lst) and lst[i] else ["", False, None, None, False]) for i in range(16)]
    hdr = "timestamp"
    for p in ("s", "h"):
        hdr += "".join(f";{p}{i}" + (f"_{s[0]}" if s[1] and s[0] else "") for i, s in enumerate(slots(name)))
    hdr += ";press\n"
    rows = ""
    for n in (name, "fail0"):
        ts, press, _ = SENSOR_RECORDS[n]
        s = slots(n)
        rows += (str(ts) + "".join(";" + (x[2] or "") for x in s) + "".join(";" + (x[3] or "") for x in s)
                 + ";" + (press or "") + "\n")
    return hdr + rows


def firmware():
    """What the preview is held to, read off the firmware and its test:
    (data for the harness, failures found before node runs)."""
    h = ALARM_H.read_text(encoding="utf-8")
    test = ALARM_TEST.read_text(encoding="utf-8")
    storage = STORAGE.read_text(encoding="utf-8")
    walker = h[h.find("inline int alarmFormatLine("):]
    tokens = re.findall(r'memcmp\(tpl \+ ti, "(\{\w+\})", \d+\)', walker)
    codes = []
    for fn in ("alarmCodeAlarmField", "alarmCodeErrField", "alarmCodeMaintField"):
        body = h[h.find(f"inline const char* {fn}("):]
        body = body[:body.find("\n}")]
        codes += re.findall(r'case ALARM_ERR_\w+:\s*return "(\w+)";', body)
    demo_cfg = test.find("static void fillDemoCfg")
    tpl = dict(ALARM_TEMPLATES_LITERAL)
    tpl["t21"] = c_string(test[max(demo_cfg, 0):], "strncpy(cfg.alarmTel.lineTemplate,", ALARM_TEST.name)
    tpl["v24"] = c_string(test, "V24_TEMPLATE =", ALARM_TEST.name)
    default_alarm = c_string(storage, "ALARM_LINE_TEMPLATE_DEFAULT =", STORAGE.name)
    data = {
        "tokens": tokens, "codes": codes, "carries": CARRIES, "tpl": tpl,
        "alarm": ALARM_VECTORS, "alarmCsv": ALARM_CSV_VECTORS, "alarmCsvDerived": ALARM_CSV_DERIVED,
        "rec": SENSOR_RECORDS, "serial": SERIAL, "json": SENSOR_JSON, "custom": SENSOR_CUSTOM,
        "csv": {n: csv_expected(n) for n in ("two", "inactive")}, "env": ENVELOPE,
        "dev": {"config": DEV_CONFIG, "status": DEV_STATUS, "alarms": DEV_ALARMS, "user": DEV_USER},
        "def": {"glob": c_string(storage, "safeCopy(_currentConfig.telGlobalTemplate,", STORAGE.name),
                "line": c_string(storage, "safeCopy(_currentConfig.telLineTemplate,", STORAGE.name),
                "sep": c_string(storage, "safeCopy(_currentConfig.telLineSeparator,", STORAGE.name),
                "aline": default_alarm},
    }
    fails = []
    if len(tokens) < 20:
        fails.append(f"only {len(tokens)} tokens read off alarmFormatLine; did AlarmPayload.h change shape?")
    if len(codes) < 10:
        fails.append(f"only {len(codes)} codes read off AlarmPayload.h; did its switch statements change shape?")
    for c in codes:
        if c not in CARRIES:
            fails.append(f"the firmware has the code {c}, and CARRIES does not say which fields it carries")
    if demo_cfg < 0:
        fails.append("test_alarm_queue has no fillDemoCfg, whose template the t21 vectors use")
    if tpl["v24"] != default_alarm:
        fails.append("the firmware's default alarm template is no longer test_alarm_queue's "
                     "V24_TEMPLATE, so its vectors describe another template")
    for want in [v[2] for v in ALARM_VECTORS] + [v[1] for v in ALARM_CSV_VECTORS]:
        if '"' + want.replace('"', '\\"') + '"' not in test:
            fails.append(f"{want} is no longer a vector of test_alarm_queue; copy the new one here")
    return data, fails


def wiring(src: str) -> list:
    """What node cannot see, read off the source: where the device comes from."""
    fails = []
    lang, tel = block(src, "LANG_JS"), block(src, "TEL_PAGE")
    if "window.sessReady = window.initSession()" not in lang or "return { user: user, status: sd }" not in lang:
        fails.append("lang.js does not hand the session (user and /api/status) to the page")
    if "window.sessReady" not in tel or "'/api/alarms'" not in tel:
        fails.append("TEL_PAGE does not read the session and /api/alarms")
    if "_devSerial" in tel or "_sensorDemoBatch" in tel or "SIMUT_Demo" in tel:
        fails.append("TEL_PAGE still carries the demo device")
    for pid, key in (("preview", "tel_pv_live"), ("apreview", "al_pv_live")):
        if f'id="{pid}L"' not in tel or f'data-i18n="{key}"' not in tel:
            fails.append(f"TEL_PAGE has no #{pid}L caption ({key})")
    for pack in sorted((REPO / "data" / "lang").glob("*.lng")):
        t = pack.read_text(encoding="utf-8").split("\n")
        d = json.loads(t[t.index("@WEBDICT") + 1])
        for key in ("tel_pv_live", "al_pv_live"):
            if key not in d:
                fails.append(f"{pack.name} has no @WEBDICT {key}: the caption stays in English there")
    return fails


def run(label: str, code: str, data: dict) -> bool:
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node is not installed; the preview cannot be exercised")
    r = subprocess.run([node, "-e", code + "\n" + HARNESS.replace("__DATA__", json.dumps(data))],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        print(f"  FAIL {label}: node rejected it\n{r.stderr[-2000:]}")
        return False
    fails = json.loads(r.stdout.strip().splitlines()[-1])
    for f in fails:
        print(f"  FAIL {label}: {f}")
    if not fails:
        print(f"  ok   {label}: {len(data['tokens'])} alarm tokens, "
              f"{len(data['alarm']) + len(data['alarmCsv'])} firmware vectors, "
              f"{len(data['json']) + len(data['custom']) + 2 + len(data['env'])} measurement vectors, "
              f"{len(data['codes'])} alarm codes, a device from its four API answers")
    return not fails


def main() -> int:
    src = WEBUI.read_text(encoding="utf-8", errors="replace")
    fails = wiring(src)
    data, ffails = firmware()
    for f in fails + ffails:
        print(f"  FAIL {'wiring' if f in fails else 'firmware'}: {f}")
    ok = not fails and not ffails
    if not fails:
        print("  ok   wiring: the session and /api/alarms reach the page, captions in both packs")
    tel = block(src, "TEL_PAGE")
    code = "\n".join(js_function(tel, n) for n in FUNCS)
    ok = run("as written", code, data) and ok
    ok = run("minified by build_webui_gz.py", generator()["_minify_js"](code), data) and ok
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
