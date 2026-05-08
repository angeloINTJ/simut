#!/usr/bin/env python3
"""capture_browser_screenshots.py — captura todas as páginas web via Selenium.

Pré-requisitos:
    pip install selenium  (ou apt install python3-selenium)
    chromedriver no PATH (ou ChromeDriver compatível)
    Device online com WiFi + HTTP 200

Uso:
    F9_PASS=<senha> python3 tools/manual_capture/capture_browser_screenshots.py

Output:
    docs/screenshots/web_*.png
"""
import os
import sys
import time
import hashlib
import urllib.request
import urllib.parse
import json

try:
    from selenium import webdriver
    from selenium.webdriver.chrome.options import Options
    from selenium.webdriver.common.by import By
    from selenium.webdriver.support.ui import WebDriverWait
    from selenium.webdriver.support import expected_conditions as EC
except ImportError:
    print("FATAL: selenium not installed. Run: pip install selenium")
    sys.exit(1)

SIMUT_IP = os.getenv("SIMUT_IP", "192.168.3.195")
F9_PASS = os.getenv("F9_PASS", "F9Test@2026")
OUT_DIR = "docs/screenshots"

os.makedirs(OUT_DIR, exist_ok=True)

# Configure headless Chrome
opts = Options()
opts.add_argument("--headless=new")
opts.add_argument("--window-size=1280,800")
opts.add_argument("--disable-gpu")
opts.add_argument("--no-sandbox")

print(f"Starting headless Chrome (target: http://{SIMUT_IP})")
driver = webdriver.Chrome(options=opts)
wait = WebDriverWait(driver, 10)

try:
    # 1. Login page screenshot (before login)
    driver.get(f"http://{SIMUT_IP}/login")
    time.sleep(2)
    driver.save_screenshot(f"{OUT_DIR}/web_login.png")
    print("  saved web_login.png")

    # Login via form
    user_field = wait.until(EC.presence_of_element_located((By.ID, "user")))
    pass_field = driver.find_element(By.ID, "pass")
    user_field.send_keys("admin")
    pass_field.send_keys(F9_PASS)
    driver.find_element(By.ID, "submit").click()
    time.sleep(3)

    # 2. Dashboard
    driver.get(f"http://{SIMUT_IP}/")
    time.sleep(3)
    driver.save_screenshot(f"{OUT_DIR}/web_dashboard.png")
    print("  saved web_dashboard.png")

    # 3+ Demais páginas
    pages = [
        ("history", "/history"),
        ("alarms", "/alarms"),
        ("config", "/config"),
        ("network", "/network"),
        ("users", "/users"),
        ("files", "/files"),
        ("license", "/license"),
    ]
    for name, url in pages:
        driver.get(f"http://{SIMUT_IP}{url}")
        time.sleep(2.5)
        driver.save_screenshot(f"{OUT_DIR}/web_{name}.png")
        print(f"  saved web_{name}.png")

    print("\nDone. All screenshots em", OUT_DIR)
    print("Files:")
    for f in sorted(os.listdir(OUT_DIR)):
        if f.startswith("web_"):
            size = os.path.getsize(os.path.join(OUT_DIR, f))
            print(f"  {f}: {size} bytes")

finally:
    driver.quit()
