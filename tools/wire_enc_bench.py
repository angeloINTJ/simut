#!/usr/bin/env python3
"""wire_enc_bench.py — o ENC28J60 do SIMUT Wire na bancada, antes de o SIMUT o dirigir.

Roda a bancada da propria biblioteca PicoENC28J60 (extras/bench: o BenchTest e o bench.py
provam o driver, do SPI por PIO ao chip perdido; o BenchLwip e o benchlwip.py provam a
interface lwIP, do DHCP ao TCP em massa e ao cabo puxado) nos pinos que o SIMUT da ao
modulo e no commit da biblioteca que o Wire vai usar. E a prova de que a fiacao, o modulo
e a biblioteca funcionam juntos nesses pinos; o firmware do SIMUT nao entra.

Os pinos nao sao copiados: saem do src/simut_config.h (secao 10b), pelo pre-processador do
host, com os flags que o manifesto da a imagem pico_wire (o INT e o GP11 com qualquer
mostrador). Os sketches da bancada usam os pinos padrao do ENC28J60Config; este
roteiro copia a biblioteca daquele commit para uma pasta temporaria, troca ali os cinco
padroes (MOSI, MISO, SCK, CS e o RESET, que o Wire nao liga: -1) e roda os roteiros da
biblioteca a partir da copia, que compilam contra ela. Cada troca tem de achar a sua linha
exatamente uma vez, senao nada roda.

A placa e achada so pelo numero de serie (--target-serial, ou SIMUT_TARGET_SERIAL), nunca
por modelo: uma imagem para o Pico comum enumera como "Pico", como a mao antiga que segue
no USB deste PC noutro projeto, e --flash grava o que achar.

  # so compilar os dois sketches, para gravar a mao (BOOTSEL e arrastar o .uf2):
  python3 tools/wire_enc_bench.py --build-only
  # rodar (a placa ja com o sketch certo, ou --flash para o arduino-cli gravar):
  python3 tools/wire_enc_bench.py --suite raw  --target-serial <serial> --ip 192.168.3.77
  python3 tools/wire_enc_bench.py --suite lwip --target-serial <serial> --static-ip 192.168.3.78

O raw precisa de um IP livre da LAN (o BenchTest nao usa DHCP) e o lwip, de um servidor
DHCP e de um IP livre para o teste de IP fixo; um endereco que responde ping e recusado. O
PC tem de estar na mesma LAN do modulo. A saida de cada rodada vai para --out, com data.
"""
import argparse
import datetime
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402

# A biblioteca no commit que o mantenedor fixou em 2026-10-12 (a main de entao, 14 commits
# depois da v1.2.0). Quando o driver do Wire a puser no lib_deps, este numero sai de la.
LIB_URL = "https://github.com/angeloINTJ/pico-enc28j60.git"
LIB_SHA = "fb16ec132baab989b5921075dd1f1503dd8149d4"
CACHE = os.path.expanduser("~/.cache/simut/pico-enc28j60")
# O Pico comum. Num Pico W a imagem roda igual: o radio fica desligado, porque nada sobe o
# WL_ON (GP23), e os pinos do modulo nao encostam nos do radio (GP23, 24, 25 e 29).
FQBN = "rp2040:rp2040:rpipico"
DEFAULT_OUT = os.path.join(ROOT, "scratchpad", "wire_" + datetime.date.today().strftime("%Y%m%d"))

# Placas que nunca sao o alvo, pelo numero de serie (AGENTS.md secao 1).
NOT_TARGETS = {"E660C062131E3E27": "a mao antiga, que segue no USB noutro projeto"}

PROBE = ('#include "simut_config.h"\n'
         "PROBE_PINS SIMUT_ENC_MISO SIMUT_ENC_CS SIMUT_ENC_SCK SIMUT_ENC_MOSI SIMUT_ENC_INT_PIN\n")
SUITES = {"raw": ("BenchTest", "bench.py"), "lwip": ("BenchLwip", "benchlwip.py")}


def die(msg):
    sys.exit(f"wire_enc_bench: {msg}")


def wire_pins():
    """{'miso','cs','sck','mosi','int'} como o simut_config.h os da a imagem pico_wire."""
    if shutil.which("cpp") is None:
        die("nao ha `cpp` no host: os pinos saem do simut_config.h pelo pre-processador (instale o gcc)")
    M = gf.load_manifest()
    composed = gf.compose(gf.resolve_profile("pico_wire", M["profiles"]), M)
    flags = list(composed["flags"]) + ["-DARDUINO_ARCH_RP2040"]
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, "pins.c")
        with open(probe, "w", encoding="utf-8") as fh:
            fh.write(PROBE)
        r = subprocess.run(["cpp", "-P", f"-I{SRC}"] + flags + [probe], capture_output=True, text=True)
    line = next((ln for ln in r.stdout.splitlines() if ln.startswith("PROBE_PINS")), None)
    if r.returncode != 0 or line is None:
        die(f"o pre-processador recusou o simut_config.h com os flags da pico_wire: {r.stderr.strip()[:400]}")
    nums = [int(n) for n in re.findall(r"-?\d+", line.replace("(", " ").replace(")", " "))]
    if len(nums) != 5:
        die(f"esperava cinco pinos do simut_config.h, li {line!r}")
    return dict(zip(("miso", "cs", "sck", "mosi", "int"), nums))


def library_copy(dst):
    """A biblioteca no LIB_SHA, copiada para dst. O cache fica em ~/.cache (o /tmp some num
    reboot) e e recusado se tiver qualquer mudanca local."""
    git = ["git", "-C", CACHE]
    if not os.path.isdir(os.path.join(CACHE, ".git")):
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        subprocess.run(["git", "clone", "--quiet", LIB_URL, CACHE], check=True)
    head = subprocess.run(git + ["rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != LIB_SHA:
        subprocess.run(git + ["fetch", "--quiet", "origin"], check=True)
        subprocess.run(git + ["checkout", "--quiet", "--detach", LIB_SHA], check=True)
        head = subprocess.run(git + ["rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != LIB_SHA:
        die(f"o cache {CACHE} esta em {head}, nao em {LIB_SHA}")
    dirty = subprocess.run(git + ["status", "--porcelain"], capture_output=True, text=True).stdout.strip()
    if dirty:
        die(f"o cache {CACHE} tem mudancas locais; apague-o para baixar de novo:\n{dirty}")
    shutil.copytree(CACHE, dst, ignore=shutil.ignore_patterns(".git"))


def set_pins(copy, pins):
    """Troca os padroes de pino do ENC28J60Config na copia. Cada campo tem de aparecer uma vez
    so, no formato `int8_t <campo> = <n>;`: se a biblioteca mudou de forma, nada roda."""
    header = os.path.join(copy, "src", "PicoENC28J60.h")
    with open(header, encoding="utf-8") as fh:
        text = fh.read()
    want = {"mosi_pin": pins["mosi"], "miso_pin": pins["miso"], "sclk_pin": pins["sck"],
            "cs_pin": pins["cs"], "rst_pin": -1}
    for field, value in want.items():
        pat = re.compile(rf"^(\s*int8_t {field} = )-?\d+;.*$", re.M)
        hits = pat.findall(text)
        if len(hits) != 1:
            die(f"{header}: `int8_t {field} = N;` aparece {len(hits)} vezes; a biblioteca mudou de forma")
        text = pat.sub(lambda m, v=value: f"{m.group(1)}{v}; // SIMUT Wire (src/simut_config.h)", text)
    with open(header, "w", encoding="utf-8") as fh:
        fh.write(text)


# Duas sondagens do BenchTest supoem a fiacao padrao da biblioteca, e nos pinos do Wire
# mediriam outra coisa. A do "sem chip" poe o CS no GP15, que la nao vai a lugar nenhum e
# aqui e o MOSI: o begin( ) recusaria a configuracao (dois sinais num pino) em vez de dizer
# que nao ha chip. E o caso "INT na linha de reset" faz irq_pin = rst_pin, que com o RST do
# Wire (-1) vira "sem INT e sem RST", uma configuracao valida. Na copia, a primeira vai para
# o GP8 e a segunda ganha um RST (GP9): slots de sensor sem nada na bancada (o DHT22 e o
# GP10), livres com qualquer mostrador. Ate 2026-10-12 eram o GP27 e o GP26, os pinos do
# modo I2C do LCD, que com a TFT sao o DC e o RST dela.
FREE_PINS = (8, 9)
# E o PIOSHARE cria sete seriais por PIO (so TX) nos GP10 a GP16, livres na fiacao padrao e,
# no Wire, o SPI do modulo (GP12-15), o DHT22 (GP10) e o RS do LCD (GP16): a serial no MISO
# fazia a revisao ler 0xFF. Na copia elas vao para os GP0 a GP6, slots de sensor sem nada.
PROBE_FIXES = (
    ("users[i] = new (storage[i]) SerialPIO(10 + i, NOPIN);",
     "users[i] = new (storage[i]) SerialPIO(0 + i, NOPIN);  // SIMUT Wire: GP0-6, ligados a nada"),
    ("    wrong.cs_pin = 15;\n",
     f"    wrong.cs_pin = {FREE_PINS[0]};  // SIMUT Wire: um pino ligado a nada\n"),
    ("{[](ENC28J60Config& c) { c.irq_pin = c.rst_pin; }},",
     f"{{[](ENC28J60Config& c) {{ c.rst_pin = {FREE_PINS[1]}; c.irq_pin = c.rst_pin; }}}},"),
)


def fix_probes(copy):
    """As duas sondagens do BenchTest, ajustadas aos pinos do Wire; cada ancora, uma vez so."""
    sketch = os.path.join(copy, "extras", "bench", "BenchTest", "BenchTest.ino")
    with open(sketch, encoding="utf-8") as fh:
        text = fh.read()
    for old, new in PROBE_FIXES:
        if text.count(old) != 1:
            die(f"{sketch}: {old.strip()!r} aparece {text.count(old)} vezes; a bancada da biblioteca mudou")
        text = text.replace(old, new)
    with open(sketch, "w", encoding="utf-8") as fh:
        fh.write(text)


def target_port(serial, hand_serial):
    """O /dev/serial/by-id da placa com esse numero de serie, e so ela."""
    if not serial:
        die("diga qual placa e o alvo: --target-serial (ou SIMUT_TARGET_SERIAL); nenhum modelo e adivinhado")
    serial = serial.upper()
    if serial in NOT_TARGETS:
        die(f"{serial} e {NOT_TARGETS[serial]}: nunca e o alvo")
    if hand_serial and serial == hand_serial.upper():
        die(f"{serial} e a PicoHand (PICO_HAND_SERIAL): nunca e o alvo")
    ports = sorted(glob.glob(f"/dev/serial/by-id/*_{serial}-if00"))
    if len(ports) != 1:
        found = ", ".join(os.path.basename(p) for p in glob.glob("/dev/serial/by-id/*")) or "nenhuma"
        die(f"{len(ports)} portas com o serial {serial} (no USB: {found})")
    return ports[0]


def ip_is_free(ip):
    """Um endereco que responde ping nao esta livre."""
    if shutil.which("ping") is None:
        return True
    r = subprocess.run(["ping", "-c", "2", "-W", "1", ip], capture_output=True)
    return r.returncode != 0


def compile_sketch(copy, sketch, irq, out):
    """Compila o sketch como o flash( ) da bancada da biblioteca o compila, e guarda o .uf2."""
    build = tempfile.mkdtemp(prefix=f"wire-{sketch}-")
    cmd = ["arduino-cli", "compile", "--fqbn", FQBN, "--library", copy, "--build-path", build]
    if irq is not None:
        cmd += ["--build-property", f"compiler.cpp.extra_flags=-DBENCH_IRQ_PIN={irq}"]
    r = subprocess.run(cmd + [os.path.join(copy, "extras", "bench", sketch)], capture_output=True, text=True)
    if r.returncode != 0:
        die(f"o arduino-cli nao compilou {sketch}:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    uf2 = os.path.join(build, f"{sketch}.ino.uf2")
    dst = os.path.join(out, f"{sketch}_{'irq%d' % irq if irq is not None else 'poll'}.uf2")
    shutil.copyfile(uf2, dst)
    with open(dst, "rb") as fh:
        sha = hashlib.sha256(fh.read()).hexdigest()
    return dst, sha


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--suite", choices=sorted(SUITES), help="raw (BenchTest/bench.py) ou lwip (BenchLwip/benchlwip.py)")
    ap.add_argument("--build-only", action="store_true", help="so compilar os dois sketches e guardar os .uf2")
    ap.add_argument("--target-serial", default=os.environ.get("SIMUT_TARGET_SERIAL"))
    ap.add_argument("--poll", action="store_true", help="sem o pino de INT (o driver consulta o chip)")
    ap.add_argument("--flash", action="store_true", help="o arduino-cli grava o sketch da suite no alvo antes")
    ap.add_argument("--ip", help="raw: um IP livre da LAN para o BenchTest")
    ap.add_argument("--static-ip", help="lwip: um IP livre da LAN para o teste de IP fixo")
    ap.add_argument("--tests", help="so estes testes da suite, separados por virgula")
    ap.add_argument("--out", default=DEFAULT_OUT, help=f"onde guardar os .uf2 e as saidas (padrao {DEFAULT_OUT})")
    args = ap.parse_args()
    if not args.build_only and not args.suite:
        ap.error("diga --suite raw|lwip, ou --build-only")

    pins = wire_pins()
    irq = None if args.poll else (pins["int"] if pins["int"] >= 0 else None)
    print(f"pinos do simut_config.h (pico_wire): MISO GP{pins['miso']}, CS GP{pins['cs']}, SCK GP{pins['sck']}, "
          f"MOSI GP{pins['mosi']}, INT {'GP%d' % irq if irq is not None else 'nenhum (consulta)'}, RESET nenhum")
    print(f"biblioteca: {LIB_URL} @ {LIB_SHA[:7]}; placa {FQBN}")
    os.makedirs(args.out, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="wire-enc-") as tmp:
        copy = os.path.join(tmp, "pico-enc28j60")
        library_copy(copy)
        set_pins(copy, pins)
        fix_probes(copy)

        if args.build_only:
            for sketch, _ in SUITES.values():
                dst, sha = compile_sketch(copy, sketch, irq, args.out)
                print(f"{dst}  sha256 {sha[:16]}")
            return 0

        if shutil.which("arduino-cli") is None and args.flash:
            die("--flash precisa do arduino-cli com o core rp2040")
        port = target_port(args.target_serial, os.environ.get("PICO_HAND_SERIAL"))
        sketch, script = SUITES[args.suite]
        cmd = [sys.executable, os.path.join(copy, "extras", "bench", script), "--port", port, "--fqbn", FQBN]
        if irq is not None:
            cmd += ["--irq", str(irq)]
        if args.flash:
            cmd.append("--flash")
        if args.tests:
            cmd += ["--tests", args.tests]
        if args.suite == "raw":
            if not args.ip:
                die("a suite raw precisa de --ip, um endereco livre da LAN (o BenchTest nao usa DHCP)")
            if not ip_is_free(args.ip):
                die(f"{args.ip} responde ping: nao esta livre")
            cmd += ["--ip", args.ip]
        elif args.static_ip:
            if not ip_is_free(args.static_ip):
                die(f"{args.static_ip} responde ping: nao esta livre")
            cmd += ["--static-ip", args.static_ip]

        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        log = os.path.join(args.out, f"enc_{args.suite}_{'irq%d' % irq if irq is not None else 'poll'}_{stamp}.log")
        print(f"alvo: {port}\nrodando: {' '.join(cmd[1:])}\nsaida: {log}", flush=True)
        with open(log, "w", encoding="utf-8") as fh:
            fh.write(f"# {stamp} {sketch} pins={pins} irq={irq} lib={LIB_SHA} fqbn={FQBN} port={port}\n")
            fh.flush()
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
            for line in proc.stdout:
                sys.stdout.write(line)
                fh.write(line)
            code = proc.wait()
            fh.write(f"# exit {code}\n")
        print(f"wire_enc_bench: {args.suite} terminou com {code} ({'passou' if code == 0 else 'FALHOU'}); saida em {log}")
        return code


if __name__ == "__main__":
    sys.exit(main())
