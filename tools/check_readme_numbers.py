#!/usr/bin/env python3
"""The numbers the three READMEs quote are the numbers the tree measures.

WHY THIS EXISTS
---------------
The READMEs cite counts that other files already own: how many host test cases
run in how many suites, how full the release image is, how many event codes
and HTTP routes exist. Each was right on the day someone typed it, and nothing
noticed when it stopped being right. On 2026-10-01, re-reading the v2.8.0
README against the tree after an outside review had quoted it back:

  * "426 host test cases in 8 suites" — the suites ran 428;
  * the "Host tests" block listed seven suites adding up to 408 — native_sensors
    was missing, and three of the seven counts had moved (185 -> 190, 31 -> 33,
    39 -> 43);
  * "The release image uses 97.2 % of the program slot, and its .bin sits
    13,196 B under the over-the-air ceiling" — the v2.7.1 figures, in all three
    languages, while tools/flash_budget.json said 95.7 % and the .bin had
    28,996 B of slack.

The review that quoted them took the stale flash figure as the state of the
project. A number in a README is a claim, and this house treats a claim that no
longer matches the code as a defect.

WHAT IT CHECKS, in README.md, README.pt-BR.md and README.es-ES.md
----------------------------------------------------------------
  1. the total of host test cases and the number of suites, against the
     RUN_TEST( ) lines of each native environment's suite in platformio.ini,
     in every .cpp of its folder (test_ota_sig keeps the slot writer's cases
     in test_slot.cpp since 2026-10-03, and counting test_main.cpp alone
     left them out);
  2. the "Host tests" block: every native environment listed once, each with
     its own count;
  3. the release image's share of the program slot, against "measured" and
     "ceiling" in tools/flash_budget.json, to one decimal;
  4. the number of event codes, against tools/logcodes.tsv;
  5. the route counts (total, gated, public by design), against what
     tools/check_authz.py reports.

A sentence the gate cannot find is a failure too: rewording the README must
move the pattern here, or the check would pass by checking nothing.

@project SIMUT
@license MIT License
"""
import configparser
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# One pattern per fact and language. Group 1 (and 2, 3) capture the numbers.
SENTENCES = {
    "README.md": {
        "total": r"runs (\d[\d,]*) host test cases in (\d+) suites",
        "flash": r"The release image uses (\d+\.\d) % of the 1,044,480 B program slot",
        "codes": r"(\d+) event codes",
        "routes": r"\*\*(\d+) routes\*\* — (\d+) gated by a permission, (\d+) public by design",
        "routes_short": r"\*\*HTTP API\*\* — (\d+) routes\.",
        "block_unit": r"(?: cases)?",
    },
    "README.pt-BR.md": {
        "total": r"roda (\d[\d.]*) casos de teste no host em (\d+) suítes",
        "flash": r"A imagem release usa (\d+,\d) % do slot de programa de 1\.044\.480 B",
        "codes": r"(\d+) códigos de evento",
        "routes": r"\*\*(\d+) rotas\*\* — (\d+) protegidas por permissão, (\d+) públicas por projeto",
        "routes_short": r"\*\*API HTTP\*\* — (\d+) rotas\.",
        "block_unit": r"(?: casos)?",
    },
    "README.es-ES.md": {
        "total": r"ejecuta (\d[\d.]*) casos de test en el host en (\d+) suites",
        "flash": r"La imagen release usa el (\d+,\d) % del slot de programa de 1\.044\.480 B",
        "codes": r"(\d+) códigos de evento",
        "routes": r"\*\*(\d+) rutas\*\* — (\d+) protegidas por un permiso, (\d+) públicas por diseño",
        "routes_short": r"\*\*API HTTP\*\* — (\d+) rutas\.",
        "block_unit": r"(?: casos)?",
    },
}

RUN_TEST = re.compile(r"^\s*RUN_TEST\(", re.M)


def native_suites():
    """{env: test case count} for every [env:native*] in platformio.ini."""
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.read(ROOT / "platformio.ini", encoding="utf-8")
    out = {}
    for sec in cfg.sections():
        if not sec.startswith("env:native"):
            continue
        env = sec[len("env:"):]
        filt = cfg[sec].get("test_filter", "").strip()
        if not filt:
            continue
        out[env] = sum(len(RUN_TEST.findall(src.read_text(encoding="utf-8")))
                       for src in sorted((ROOT / "test" / filt).glob("*.cpp")))
    return out


def release_share():
    d = json.loads((ROOT / "tools" / "flash_budget.json").read_text(encoding="utf-8"))
    return round(100.0 * d["envs"]["pico_w_release"]["measured"] / d["ceiling"], 1)


def log_codes():
    lines = (ROOT / "tools" / "logcodes.tsv").read_text(encoding="utf-8").splitlines()
    return sum(1 for l in lines if re.match(r"^\d+\t", l))


def routes():
    """(total, gated, public) as tools/check_authz.py prints them."""
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "check_authz.py")],
                       capture_output=True, text=True, cwd=ROOT)
    m = re.search(r"\((\d+) routes — (\d+) gated, (\d+) public-by-design\)", r.stdout)
    if not m:
        sys.exit("[readme-numbers] could not read the route counts from check_authz.py:\n"
                 + r.stdout + r.stderr)
    return tuple(int(x) for x in m.groups())


def num(s):
    """'1,044' / '1.044' -> 1044; the separators are thousands in all three."""
    return int(s.replace(",", "").replace(".", ""))


def main():
    suites = native_suites()
    total = sum(suites.values())
    share = release_share()
    codes = log_codes()
    r_total, r_gated, r_public = routes()
    errors = []

    for name, pat in SENTENCES.items():
        text = (ROOT / name).read_text(encoding="utf-8")

        def find(key):
            m = re.search(pat[key], text)
            if not m:
                errors.append(f"{name}: no sentence matches /{pat[key]}/ — reworded? "
                              f"move the pattern in tools/check_readme_numbers.py")
            return m

        m = find("total")
        if m and (num(m.group(1)), int(m.group(2))) != (total, len(suites)):
            errors.append(f"{name}: says {m.group(1)} cases in {m.group(2)} suites; "
                          f"the suites run {total} cases in {len(suites)}")

        block = {}
        for bm in re.finditer(r"pio test -e (native\w*)\s+#[^\n]*\((\d+)" + pat["block_unit"]
                              + r"\)", text):
            if bm.group(1) in block:
                errors.append(f"{name}: '{bm.group(1)}' listed twice in the host-test block")
            block[bm.group(1)] = int(bm.group(2))
        for env, n in sorted(suites.items()):
            if env not in block:
                errors.append(f"{name}: the host-test block does not list {env} ({n} cases)")
            elif block[env] != n:
                errors.append(f"{name}: the host-test block gives {env} {block[env]} cases; "
                              f"it has {n}")
        for env in sorted(set(block) - set(suites)):
            errors.append(f"{name}: the host-test block lists {env}, which platformio.ini "
                          f"does not define")

        m = find("flash")
        if m and float(m.group(1).replace(",", ".")) != share:
            errors.append(f"{name}: says the release image uses {m.group(1)} % of the slot; "
                          f"tools/flash_budget.json measures {share} %")

        m = find("codes")
        if m and int(m.group(1)) != codes:
            errors.append(f"{name}: says {m.group(1)} event codes; tools/logcodes.tsv has {codes}")

        m = find("routes")
        if m and tuple(int(x) for x in m.groups()) != (r_total, r_gated, r_public):
            errors.append(f"{name}: says {'/'.join(m.groups())} routes (total/gated/public); "
                          f"check_authz.py counts {r_total}/{r_gated}/{r_public}")
        m = find("routes_short")
        if m and int(m.group(1)) != r_total:
            errors.append(f"{name}: says {m.group(1)} routes; check_authz.py counts {r_total}")

    if errors:
        for e in errors:
            print(f"[readme-numbers] FAIL {e}")
        return 1
    print(f"[readme-numbers] OK: {total} cases in {len(suites)} suites, release {share} % "
          f"of the slot, {codes} event codes, {r_total} routes ({r_gated} gated, "
          f"{r_public} public), in all three READMEs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
