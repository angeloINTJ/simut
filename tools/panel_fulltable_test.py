#!/usr/bin/env python3
"""Fill every account slot and drive the panel with all of them.

What this answers, and why it needs the whole table to answer it: the panel's
scrambled keypad names THREE digits per tap, so an entry of n taps stands for
up to 3^n PINs. Identification walks that set and refuses when two accounts
both fall inside it, because the panel cannot ask which was meant. With one or
two accounts that never happens. With the table full it is the failure that
actually happens, and how often is arithmetic nobody should have to trust:

    P(some other account is in the set) ~= 1 - (1 - 3^n/10^n)^(accounts-1)

which for 32 accounts on 4-digit PINs was about a fifth of all logins, and for
6-digit PINs about one in fifty. This script measured 15% and 4% on 19/09.

v25 removed the cause rather than the symptom: the account is picked BEFORE
the PIN, so the tap tree is resolved against ONE digest and no entry can fit
two accounts. The same run now asserts the opposite — 311 SEC_PIN_AMBIGUOUS
must stay at ZERO with the table full and four-digit PINs, which is also the
only way to tell the fix from a bench that stopped exercising the case.

It also checks the ordinary things at scale: that 32 accounts can be created,
that each one identifies as ITSELF (log 308 with its own slot), that each can
do exactly what its bits allow and nothing else, and that every action reaches
the event log and the alarm line signed with the right name.

Usage:
    python3 tools/alarm_collector.py --port 18081 --log /tmp/alarms.jsonl &
    python3 tools/panel_fulltable_test.py --out shots/ --collector-log /tmp/alarms.jsonl
    python3 tools/panel_fulltable_test.py --out shots/ --pin-len 6   # the mitigation

A login costs one keypad read per digit, because the deal is re-rolled on
every tap. Both reads go over HTTP (`/api/keypad`, `/api/logs`), and that is
not a nicety: over the serial CLI the same reads cost 1.2 s and 1.8 s, a
4-digit entry took 31.1 s against 4.6 s, and an 8-tap entry ran past the
panel's 30 s idle guard, which sends the screen home mid-PIN. The first two
full-table attempts died that way at accounts 18 and 21 of 25. Measured on the
rig 19/09 with 32 accounts and 1189 log records.
"""
import argparse
import json
import os
import random
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from panel_users_hw_test import (  # noqa: E402
    Rig, Collector, check, host_ip, COLLECTOR_PORT, COLLECTOR_LOG,
    CFG_BTN, ROW_Y, FOOT, ALARMS_EXIT, MSG_OK,
    PIN_GRID_FALLBACK, PIN_OK, PIN_CANCEL,
    MAINT_ROW, MAINT_DEC, MAINT_INC, MAINT_MID,
)

PERM_LIM, PERM_BLK, PERM_MNT, PERM_USR = 0x0400, 0x0800, 0x1000, 0x0100
MAX_USERS = 32                      # SystemDefs_Limits.h

# One account per slot 1..31. The mix is deliberate: every single bit alone,
# every pair, all three, none at all, and one with USER_MGR — so that "what
# the menu offers" is exercised across the whole table and not just twice.
def plan_accounts(n, pin_len, rng):
    bits = [PERM_LIM, PERM_BLK, PERM_MNT,
            PERM_LIM | PERM_BLK, PERM_LIM | PERM_MNT, PERM_BLK | PERM_MNT,
            PERM_LIM | PERM_BLK | PERM_MNT]
    lo, hi = 10 ** (pin_len - 1), 10 ** pin_len - 1
    pins, used = [], set()
    while len(pins) < n:
        p = str(rng.randint(lo, hi))
        if p not in used and len(p) == pin_len:
            used.add(p)
            pins.append(p)
    out = []
    for i in range(n):
        if i == n - 1:
            perm = 0                       # no panel bit: the menu must not offer Alarms
        elif i == n - 2:
            perm = PERM_USR                # the Users item, and nothing on the alarms
        else:
            perm = bits[i % len(bits)]
        out.append({'name': f'u{i + 1:02d}', 'perm': perm, 'pin': pins[i]})
    return out


def _ui_modes():
    """enum UiMode, read from the header rather than copied into this file.

    The first cut of this block wrote the numbers out by hand and put
    MODE_SETTINGS_ALARM_SENSOR at 22; it is 21. Every screen check then failed,
    every action was retried once and abandoned, and the run reported that
    three accounts had not acted when all three had. The enum grows at the end
    every time the panel gains a screen (v24 added six), so a copied number is
    a number that will be wrong later even if it is right today."""
    hdr = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       '..', 'src', 'SystemDefs_Records.h')
    body = open(hdr, encoding='utf-8', errors='replace').read()
    start = body.index('enum UiMode {')
    block = body[start:body.index('};', start)]
    names, n = {}, 0
    for line in block.splitlines()[1:]:
        line = re.sub(r'/\*.*?\*/', '', line)
        line = line.split('/**')[0].split('//')[0].strip().rstrip(',').strip()
        if re.fullmatch(r'MODE_[A-Z0-9_]+', line):
            names[line] = n
            n += 1
    missing = [k for k in ('MODE_SETTINGS_MAIN', 'MODE_SETTINGS_ALARMS',
                           'MODE_SETTINGS_ALARM_EDIT', 'MODE_SETTINGS_ALARM_SENSOR',
                           'MODE_SETTINGS_MAINT') if k not in names]
    if missing:
        raise SystemExit(f'enum UiMode: could not read {missing} from {hdr}')
    return names


_UI = _ui_modes()
UI_MODE_SETTINGS_MAIN = _UI['MODE_SETTINGS_MAIN']
UI_ALARMS = _UI['MODE_SETTINGS_ALARMS']              # the sensor LIST
UI_ALARM_EDIT = _UI['MODE_SETTINGS_ALARM_EDIT']      # the two limit bars
UI_ALARM_SENSOR = _UI['MODE_SETTINGS_ALARM_SENSOR']  # limits / block / maintenance
UI_MAINT = _UI['MODE_SETTINGS_MAINT']                # the maintenance window editor


def expect_mode(rig, want, timeout=8.0):
    """Wait until Core 1 is showing this screen. Returns False on timeout.

    This is the difference between a test and a guess. The panel is driven by
    absolute coordinates, so every tap means something on EVERY screen — a tap
    aimed at the third row of a sensor menu lands on the third row of the
    sensor LIST if the screen did not change in time, and the sensor list is
    not [0,1,2]: on this rig it is [0,1,3,4,10], so row 2 is sensor 3. That is
    not hypothetical. On 19/09 the maintenance action opened and closed a
    window on sensor 3 while the test believed it was working on sensor 0, and
    the only reason it was caught is that the log record carries the sensor in
    its ctx."""
    deadline = time.time() + timeout
    while True:
        if ui_mode(rig) == want:
            return True
        if time.time() > deadline:
            return False
        time.sleep(0.5)


def ui_mode(rig):
    """What Core 1 is showing, from `show metrics`. This and not the log is
    the evidence a login worked: LogPolicy never filters a WARN, but counting
    lines across 25 logins was wrong anyway — the screen either changed or it
    did not."""
    m = re.search(r'UI mode:\s*(\d+)', rig.cmd('show metrics', quiet_for=0.8, timeout=12))
    return int(m.group(1)) if m else -1


def login(rig, pin, who, tries=3):
    """Identify at the panel. Returns (outcome, attempts).

    `who` is the account NAME (Rig.pick_user resolves it against the picker's
    list). Never an index: the rig carries accounts this script did not
    create, and a hardcoded one picks somebody else — which on 2026-09-20 sent
    the taps to a keypad that was never opened.

    v25 changed what this function is measuring. Until v24 the PIN WAS the
    identity, so one entry stood for up to 3^n strings searched against every
    account, and the failure this whole script exists to provoke was two
    accounts falling inside the same entry — 4 retries and 4 log-311 events in
    25 logins on 19/09, 15% against 13.5% expected. Since v25 the account is
    chosen first (`idx` is its position among the accounts holding a PIN, in
    slot order — admin is 0, u01 is 1) and the tap tree is resolved against
    ONE digest, so an ambiguous entry has nowhere to happen. A retry here now
    means a lost tap, not a collision, and the run asserts 311 stays at zero.
    """
    for attempt in range(1, tries + 1):
        rig.goto('dash')
        rig.tap(*CFG_BTN, 1.2)
        rig.pick_user(who)
        rig.pin(pin)
        time.sleep(1.2)
        if ui_mode(rig) == UI_MODE_SETTINGS_MAIN:
            return 'ok', attempt
    return 'refused', tries


def act_for(rig, acc, slot_row, expect_ctx=None):
    """The one action this account's bits allow, from the alarms list.
    Returns the log code it should have produced, or None when it has no
    panel bit and the menu should not even offer Alarms.

    The limit change is ONE WAY on purpose: nudging a bar up and back down
    leaves no diff, and panelSaveAlarmLimits saves and signs the DIFF — the
    first draft of this test did exactly that and then wondered why no record
    ever reached the collector. The caller restores the limits at the end.

    Every branch VERIFIES and retries once. A tap that arrives while Core 1 is
    still repainting is charged to the screen it started on and does nothing,
    and no error says so — the screen simply did not move. Only the limits
    branch used to retry; on 19/09 two consecutive 5-account passes each lost
    a different action (limits and block in one, block and maintenance in the
    next), and in both the event log and the alarm line agreed the action had
    not happened, which is what separates a lost tap from a lost record."""
    perm = acc['perm']
    if not (perm & (PERM_LIM | PERM_BLK | PERM_MNT)):
        return None
    dbg = os.environ.get('FT_DEBUG_SHOTS')
    def shot(tag):
        if dbg:
            rig.shot(f"dbg-{acc['name']}-{tag}")

    # Which action, in the menu's own order. Choosing in that order is what
    # keeps "the row we want is the one already selected" true for an account
    # holding more than one bit — the first draft picked maintenance first and
    # then tapped a row that was not selected, which selected it and did
    # nothing else.
    code = 442 if perm & PERM_LIM else (457 if perm & PERM_BLK else 455)

    def do_limits(attempt):
        rig.tap(160, ROW_Y[0], 1.8)          # the limit editor
        if not expect_mode(rig, UI_ALARM_EDIT):
            return False                     # the tap did not open it: retry
        shot(f'3-editor-{attempt}')
        rig.tap(280, ROW_Y[0], 1.0)          # + on the focused bar
        shot(f'4-plus-{attempt}')
        rig.tap(*FOOT['enter'], 1.8)         # SAVE
        return True

    def do_block(attempt):
        # Core 0 repaints the sensor menu after each toggle and it comes back
        # selected on the same row, so a plain tap activates it again.
        # activate_row( ) must NOT be used here: it walks down from row 0.
        rig.tap(160, ROW_Y[1], 1.6)          # off
        shot(f'3-off-{attempt}')
        if not expect_mode(rig, UI_ALARM_SENSOR):
            return False
        rig.tap(160, ROW_Y[1], 1.6)          # and on again: two signed records
        shot(f'4-on-{attempt}')
        return True

    def do_maint(attempt):
        rig.tap(160, ROW_Y[2], 1.5)          # the maintenance entry screen
        if not expect_mode(rig, UI_MAINT):
            return False
        shot(f'3-maint-{attempt}')
        rig.tap(MAINT_DEC, MAINT_ROW[0], 0.8)   # hours 1 -> 0
        rig.tap(MAINT_MID, MAINT_ROW[1], 0.8)   # focus minutes
        rig.tap(MAINT_INC, MAINT_ROW[1], 0.8)   # +5
        rig.tap(*FOOT['enter'], 1.8)         # START
        shot(f'4-started-{attempt}')
        # The window is CLOSED from the sensor menu, and only from there. The
        # same coordinate on the sensor LIST is the third row of [0,1,3,4,10],
        # which is sensor 3 — on 19/09 that is exactly what happened, and the
        # run opened and closed a window on a sensor it was not testing.
        if not expect_mode(rig, UI_ALARM_SENSOR):
            return False
        rig.tap(160, ROW_Y[2], 1.6)          # back into the window
        if not expect_mode(rig, UI_MAINT):
            return False
        rig.tap(*FOOT['enter'], 1.8)         # END
        return True

    run = do_limits if code == 442 else (do_block if code == 457 else do_maint)

    def back_to_settings_main(limit=5):
        """Back out to the settings menu whatever screen we are on. A retry
        that starts from wherever the failed attempt left off is a different
        test each time, and a fixed number of back taps is a guess: a
        sub-action can fail on any of four screens."""
        for _ in range(limit):
            if ui_mode(rig) == UI_MODE_SETTINGS_MAIN:
                return True
            rig.tap(*FOOT['exit'], 1.0)
        return ui_mode(rig) == UI_MODE_SETTINGS_MAIN

    for attempt in (1, 2):
        rig.tap(*FOOT['enter'], 1.4)         # Alarms is the first item it can reach
        shot(f'1-alarms-{attempt}')
        rig.tap(160, ROW_Y[slot_row], 1.2)   # the sensor under test
        shot(f'2-sensor-menu-{attempt}')
        if not expect_mode(rig, UI_ALARM_SENSOR):
            back_to_settings_main()
            continue
        mark = len(rig.log_records())
        run(attempt)
        shot(f'5-done-{attempt}')
        back_to_settings_main()
        if expect_ctx is None:
            break
        # Records written since this attempt started, not `[-1]` of the whole
        # log: an identical ctx left over from an earlier run makes `[-1]`
        # match at once and the retry never happens, which is how a lost save
        # looked like a good one. wait_for_log also covers the few seconds
        # /api/logs lags the RAM ring the console prints.
        _, found = rig.wait_for_log(code, expect_ctx, since=mark, timeout=20.0)
        if found:
            break
    return code


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--collector-log', default=COLLECTOR_LOG)
    ap.add_argument('--pin-len', type=int, default=4, choices=(4, 5, 6, 7, 8))
    ap.add_argument('--accounts', type=int, default=MAX_USERS - 1)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--keep', action='store_true', help='do not delete the accounts at the end')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    rng = random.Random(args.seed)

    rig = Rig(args.out)
    results = []
    # The rig carries accounts of its own, so "fill the table" means fill what
    # is FREE — asking for 31 on a device that already holds seven just fails
    # six creations and calls it a defect.
    free = MAX_USERS - len(rig.users())
    n_plan = min(args.accounts, free)
    plan = plan_accounts(n_plan, args.pin_len, rng)
    t0 = time.time()
    tel_backup = {}
    try:
        # ── the table ────────────────────────────────────────────────────────
        print(f'== filling the {len(plan)} free slots with {args.pin_len}-digit PINs '
              f'(the device already held {MAX_USERS - free}) ==')
        cfgj = rig.get('/api/config').json()
        tel_backup = {k: cfgj.get(k) for k in ('t_srv', 't_port', 't_path', 't_sec', 'a_en', 'a_path', 'a_qmax')}
        ip = host_ip()
        if tel_backup.get('t_srv') == ip:
            raise SystemExit('the device is already pointed at this host: refusing to '
                             'overwrite the backup with the bench collector')
        rig.cfg(f'tel server {ip}', f'tel port {COLLECTOR_PORT}', 'tel path /tel', 'tel crypto off',
                'alarm set on', 'alarm set mode json', 'alarm set path /tel/alarm',
                # 64 is the ceiling; the panel is "in use" for most of this run,
                # so the line cannot drain and every record has to fit in RAM.
                'alarm set qmax 64')
        for a in plan:
            rig.cfg(f"user del {a['name']}",
                    f"user add {a['name']} Full2026x{a['name']}"[:60],
                    f"user perm {a['name']} {hex(a['perm'])}",
                    f"user pin {a['name']} {a['pin']}")
        rig.cmd('write memory')
        time.sleep(1.5)
        us = rig.users()
        by_name = {u['name']: u for u in us}
        made = [a for a in plan if a['name'] in by_name]
        check(results, f'all {len(plan)} accounts were created',
              len(made) == len(plan), f'{len(made)} of {len(plan)}')
        if len(plan) == free:
            check(results, f'the table is full ({MAX_USERS} active accounts)',
                  len(us) == MAX_USERS, f'{len(us)} active')
        check(results, 'every created account holds a PIN',
              all(by_name[a['name']]['pin'] for a in made), '')
        check(results, 'every created account kept its bits',
              all(by_name[a['name']]['perms'] == a['perm'] for a in made),
              json.dumps({a['name']: (hex(a['perm']), hex(by_name[a['name']]['perms']))
                          for a in made if by_name[a['name']]['perms'] != a['perm']}))
        # one more must not fit — only meaningful when the table really is full
        if len(us) == MAX_USERS:
            out = rig.cfg('user add overflow Full2026xx')[0]
            check(results, f'account {MAX_USERS + 1} is refused',
                  'slot' in out.lower() or 'max' in out.lower(), ' '.join(out.split())[:90])

        al = rig.get('/api/alarms').json()
        sensors = al.get('sensors', al) if isinstance(al, dict) else al
        # The list IS the panel's list, so element k is row k: /api/alarms emits the
        # CONFIGURED sensors in index order (WebManager_Api.cpp:368) and so does
        # _activeSensorsMap (DisplayManager_Settings.cpp:86). Do NOT filter on
        # "active" here — in this JSON that key is alarmsActive, the alarm ENABLE
        # bit (WebManager_Api.cpp:393), not "the sensor exists". On 2026-09-20 the
        # filter left only sensor 4 (the one rig sensor with alarms on) while the
        # run tapped row 0, which is sensor 0, and the firmware's correct ctx=500
        # was read as a row-vs-index defect.
        slot_row = 0
        first = sensors[slot_row]
        slot = int(first['idx'])
        lim_before = first.get('tmin')
        # The PIN policy the numbers below were measured under. v25 makes it
        # configurable, so "15% ambiguity at four digits" means nothing without
        # it, and a rig left on another policy silently measures something
        # else. /api/keypad reports it as [minLen, maxLen, keypad, alphabet].
        policy = rig.get('/api/keypad').json().get('policy')
        print(f'  PIN policy [min,max,keypad,alphabet]: {policy}')
        print(f'  sensor slot under test: {slot}')

        # ── access, one account at a time ────────────────────────────────────
        print('== access ==')
        stats = {'ok': 0, 'refused': 0}
        amb_start = rig.log_count(311)
        rot_start = rig.log_count(22)   # ver a nota no check de identificação
        retries = 0
        first_try = 0
        log_rows = []
        for i, a in enumerate(made):
            slot_of = by_name[a['name']]['id']
            outcome, attempts = login(rig, a['pin'], a['name'])
            stats[outcome] = stats.get(outcome, 0) + 1
            retries += attempts - 1
            if outcome == 'ok' and attempts == 1:
                first_try += 1
            ctxs = rig.log_ctx(308)
            ok_slot = bool(ctxs) and ctxs[-1] == slot_of
            row = {'name': a['name'], 'slot': slot_of, 'perm': hex(a['perm']),
                   'pin_len': len(a['pin']), 'outcome': outcome, 'attempts': attempts,
                   'log_ctx': ctxs[-1] if ctxs else None, 'ctx_ok': ok_slot}
            print(f"  {a['name']} slot {slot_of:2d} {hex(a['perm']):>6} -> "
                  f"{outcome:9} in {attempts} (308 ctx={row['log_ctx']})")
            if outcome == 'ok':
                if i < 3 or i == len(made) - 1:
                    rig.shot(f"ft-menu-{a['name']}")
                pre = len(rig.log_records())
                code = act_for(rig, a, slot_row, expect_ctx=slot_of * 100 + slot)
                row['action_code'] = code
                if code:
                    # Records written SINCE the action, not the last one in the
                    # whole log. ctx = account*100 + sensor is unique to this
                    # account, but `[-1]` of the whole log is only that account's
                    # record if the action actually wrote one -- and when it does
                    # not, `[-1]` quietly hands back somebody else's. The 4-digit
                    # pass of 19/09 reported u01 acting with ctx 2400, which is
                    # account slot 24: a leftover from an ABORTED earlier run
                    # still sitting in the log. A missing record has to read as
                    # missing, so the window is explicit.
                    fresh, found = rig.wait_for_log(code, slot_of * 100 + slot, since=pre)
                    row['action_ctx'] = [r['ctx'] for r in fresh if r['code'] == code] or None
                    row['log_window'] = len(fresh)
                    row['log_waited'] = not found
                else:
                    rig.shot(f"ft-menu-nobits-{a['name']}")
            log_rows.append(row)
            rig.goto('dash')

        total = len(made)
        # A ROTAÇÃO DO LOG é a explicação que custou uma investigação em
        # 20/09: uma rodada de 25 contas escreve o bastante para o log girar,
        # e um 308 que foi para o arquivo girado some do leitor enquanto a
        # AÇÃO da mesma conta (código 442, posterior) continua lá com o ctx
        # certo. Isso não é a identificação falhando — é o instrumento cego na
        # ponta antiga. A contagem vai no detalhe para que o próximo a ler não
        # tenha de descobrir de novo; o check continua EXIGINDO o 308, porque
        # afrouxá-lo transformaria um teste numa opinião.
        rotations = rig.log_count(22) - rot_start
        missing = [r for r in log_rows if r['outcome'] == 'ok' and not r['ctx_ok']]
        check(results, 'every account identified as itself (308 with its own slot)',
              not missing,
              json.dumps(missing) + (f'  [ATENCAO: {rotations} rotacoes de log nesta rodada '
                                     f'(codigo 22) — um 308 no arquivo girado nao e lido, '
                                     f'confira o ctx da ACAO da mesma conta]' if rotations else ''))
        check(results, 'every account got in within three attempts',
              stats['ok'] == total, json.dumps(stats))
        acted = [r for r in log_rows if r.get('action_code')]
        check(results, 'every action reached the event log with ctx = account*100 + slot',
              all(r.get('action_ctx') and r['action_ctx'][0] == r['slot'] * 100 + slot
                  for r in acted),
              json.dumps([(r['name'], r['action_code'], r.get('action_ctx'),
                           r['slot'] * 100 + slot) for r in acted
                          if not (r.get('action_ctx') and r['action_ctx'][0] == r['slot'] * 100 + slot)][:6]))

        # ── the alarm line ───────────────────────────────────────────────────
        print('== telemetry ==')
        rig.goto('dash')
        # Wait for the queue to DRAIN, and never call `alarm flush` here: it
        # clears the queue unconfirmed (AppManager_Commands.cpp, CMD_ALARM_FLUSH
        # -> flushAlarmQueue) — it throws the records away instead of sending
        # them. This block used to flush and then wait 20 s for what it had just
        # destroyed. The line only runs with the panel idle, so the wait starts
        # after goto('dash').
        drained, dropped, depth = False, 0, -1
        for _ in range(30):                  # up to ~150 s
            time.sleep(5)
            m = re.search(r'(?:fila|queue)\s+(\d+)/(\d+).*?(?:descartados|dropped)\s+(\d+)',
                          rig.cmd('alarm show', quiet_for=0.8, timeout=12))
            if not m:
                continue
            depth, dropped = int(m.group(1)), int(m.group(3))
            if depth == 0:
                drained = True
                break
        check(results, 'the alarm queue drained (nothing left unsent)', drained,
              f'still {depth} queued')
        check(results, 'the alarm line dropped nothing', dropped == 0,
              f'{dropped} records dropped (qmax too small for this pass)')
        time.sleep(5)
        # The collector writes {path, ts, body} and `body` is the raw batch:
        # Collector unwraps it. Parsing the outer line as if it were a record
        # finds nothing and blames the firmware — it did, for an hour.
        col = Collector(args.collector_log)
        col._pull()
        recs = [r for r in col.records if r.get('_rx', 0) >= t0]
        signers = {r.get('user') for r in recs if r.get('user')}
        expect = {r['name'] for r in acted}
        check(results, 'the alarm line carried a record for every account that acted',
              expect <= signers, f'missing: {sorted(expect - signers)[:8]} of {len(expect)}')
        # Every account ON THE DEVICE, not only the ones this run created: the
        # rig carries accounts from before the test, and a record signed by one
        # of them is correct. The first version allowed {created} | {'admin'}
        # and failed on `smap`, an account this test has no business knowing
        # about -- which an earlier cleanup had been papering over by DELETING
        # it.
        on_device = {u['name'] for u in rig.users()} | {a['name'] for a in made}
        check(results, 'every record names an account that exists',
              signers <= on_device,
              f'unknown signers: {sorted(signers - on_device)[:6]}')

        # ── what the whole point was ─────────────────────────────────────────
        amb_events = rig.log_count(311) - amb_start
        print('\n== the measurement ==')
        print(f'  accounts            : {total} (table of {MAX_USERS})')
        print(f'  PIN length          : {args.pin_len} characters')
        print(f'  logins at first try : {first_try}/{total} = {100.0*first_try/total:.0f}%')
        print(f'  extra attempts      : {retries}')
        print(f'  ambiguity events    : {amb_events} (log code 311)')
        exp = 1 - (1 - 3 ** args.pin_len / 10 ** args.pin_len) ** (total - 1)
        print(f'  v24 would have seen : {100*exp:.0f}%  (1 - (1 - 3^n/10^n)^(accounts-1))')
        # The v25 assertion. A zero here only counts alongside the first-try
        # rate: a bench that stopped reaching the keypad would also report
        # zero collisions, and that is the failure this pairing rules out.
        check(results, 'no entry fits two accounts any more (311 == 0 with the table full)',
              amb_events == 0, f'{amb_events} events, {first_try}/{total} logins at first try')

        json.dump({'plan': plan, 'rows': log_rows, 'stats': stats,
                   'ambiguity_events': amb_events, 'pin_len': args.pin_len,
                   'policy': policy,
                   'records': recs, 'results': results},
                  open(os.path.join(args.out, f'fulltable_{args.pin_len}.json'), 'w'), indent=1)
    finally:
        print('== cleanup ==')
        # the PERM_LIM accounts each nudged the first bar up one step.
        # rig.cmd and NOT rig.cfg: `sensor ... tmin` is a privileged EXEC
        # command, and inside `configure terminal` it answers "Comando requer
        # modo privilegiado" and changes nothing — which is a silent failure,
        # because the cleanup never looked at the reply.
        if 'lim_before' in locals() and lim_before is not None:
            rig.cmd(f'sensor {slot} tmin {lim_before}')
        # The PERM_MNT accounts each OPENED a maintenance window, and a window
        # left open suppresses that sensor's alarms for up to an hour after the
        # test is over. There is no CLI verb for it; the field is `maint` in
        # the alarms section, and the array lives under "sensors" inside it.
        if 'slot' in locals():
            try:
                rig.commit({'alarms': {'sensors': [{'idx': slot, 'maint': 0}]}})
            except Exception as e:
                print(f'  [cleanup] could not close maintenance on {slot}: {e}')
        if not args.keep:
            for a in plan:
                rig.cfg(f"user del {a['name']}")
            rig.cfg('user del overflow')
        b = tel_backup
        if b.get('t_srv'):
            rig.cfg(f"tel server {b['t_srv']}", f"tel port {b['t_port']}", f"tel path {b['t_path']}",
                    f"tel crypto {'on' if b.get('t_sec') else 'off'}",
                    f"alarm set {'on' if b.get('a_en') else 'off'}",
                    f"alarm set path {b['a_path']}", f"alarm set qmax {b.get('a_qmax') or 32}")
        rig.cmd('write memory')
        # `user del smap` used to live here. smap is not this test's account —
        # it predates the run, and a cleanup that deletes accounts it did not
        # create is a cleanup that destroys the bench.
        rig.goto('dash')

    passed = sum(1 for r in results if r['ok'])
    print(f'\n{passed}/{len(results)} checks passed')
    for r in results:
        if not r['ok']:
            print(f"  FAIL {r['check']}: {r['detail'][:160]}")
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    sys.exit(main())
