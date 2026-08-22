#!/usr/bin/env python3
"""capture_web_shots.py — recapture the web UI screenshots used by the README
and the GitHub Pages site, against a real device.

The TFT screens have had tools/screen_mapper.py since v2.1; the web pages were
captured by hand, which is why docs/images/web-*.png sat five minor versions
behind the interface they claimed to show. This is the missing half.

Usage:
    python3 tools/capture_web_shots.py --host 192.168.3.24 --user admin
    # password: --password, or SIMUT_WEB_PASSWORD in the environment, or a prompt

Requires Playwright driving an installed Chrome (no browser download):
    pip install playwright        # the browser itself comes from --channel chrome

It drives the real login form, so the nonce and the client-side hashing are the
page's problem, not this script's — nothing here reimplements the auth flow.
"""
import argparse
import getpass
import os
import sys

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "images")


def capture(host, user, password, out_dir, headless=True):
    from playwright.sync_api import sync_playwright

    base = host if host.startswith("http") else f"http://{host}"
    dash = os.path.join(out_dir, "web-dashboard.png")
    sens = os.path.join(out_dir, "web-sensors.png")

    with sync_playwright() as p:
        browser = p.chromium.launch(channel="chrome", headless=headless)
        page = browser.new_page(viewport={"width": 1280, "height": 900})
        page.set_default_timeout(20000)

        page.goto(f"{base}/", wait_until="domcontentloaded")
        if "login" in page.url:
            page.fill("input[type=text]", user)
            page.fill("input[type=password]", password)
            page.press("input[type=password]", "Enter")
            page.wait_for_load_state("networkidle")
        if "login" in page.url or "chpass" in page.url:
            print(f"login failed — still at {page.url}", file=sys.stderr)
            browser.close()
            return 1

        # The gauges arrive over separate fetches; a shot taken too early shows
        # a dashboard full of dashes.
        page.wait_for_timeout(3500)
        page.screenshot(path=dash)
        print(f"wrote {dash}")

        page.goto(f"{base}/config", wait_until="domcontentloaded")
        page.wait_for_timeout(3500)
        # Frame the "Sensors & GPIO" block only: the pin map plus the slot
        # table is what the caption on the site describes.
        box = page.evaluate("""() => {
          const hs = [...document.querySelectorAll('h3')];
          const start = hs.find(h => h.innerText.trim().startsWith('Sensors'));
          if (!start) return null;
          const end = hs[hs.indexOf(start) + 1];
          const r = start.getBoundingClientRect();
          const top = r.top + window.scrollY;
          const bot = end ? end.getBoundingClientRect().top + window.scrollY
                          : document.body.scrollHeight;
          return {x: r.left + window.scrollX, y: top, w: r.width, h: bot - top};
        }""")
        if box:
            pad = 18
            page.screenshot(path=sens, full_page=True, clip={
                "x": max(0, box["x"] - pad),
                "y": max(0, box["y"] - pad),
                "width": box["w"] + pad * 2,
                "height": box["h"] - pad,
            })
        else:
            print("could not find the Sensors & GPIO heading — full page instead",
                  file=sys.stderr)
            page.screenshot(path=sens)
        print(f"wrote {sens}")
        browser.close()
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="device address, e.g. 192.168.3.24")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", default=os.environ.get("SIMUT_WEB_PASSWORD"))
    ap.add_argument("--out", default=os.path.normpath(OUT_DIR))
    ap.add_argument("--show", action="store_true", help="run the browser headed")
    args = ap.parse_args()

    password = args.password or getpass.getpass(f"password for {args.user}: ")
    return capture(args.host, args.user, password, args.out, headless=not args.show)


if __name__ == "__main__":
    sys.exit(main())
