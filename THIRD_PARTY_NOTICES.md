# Third-party notices

SIMUT is distributed under the MIT License ([LICENSE](LICENSE)). Its firmware images
also carry the third-party software below. For each one: who holds the copyright,
the licence, which images carry it, and the file in [LICENSES/](LICENSES) that holds
the licence text, reproduced verbatim as those licences ask of a binary distribution.

The panel's License screen and the `/license` web page list the same components.
This file is written by `tools/gen_notices.py` from `tools/third_party.toml`: edit
those, run the script, and CI checks that the three lists still agree.

| Component | Copyright holder | Licence | Images |
|---|---|---|---|
| [Arduino-Pico core](#arduino-pico-core) | Earle F. Philhower, III | LGPL-2.1 | all images |
| [Raspberry Pi Pico SDK](#raspberry-pi-pico-sdk) | Raspberry Pi (Trading) Ltd. | BSD-3-Clause | all images |
| [TinyUSB](#tinyusb) | Ha Thach (tinyusb.org) | MIT | all images |
| [cyw43-driver](#cyw43-driver) | George Robotics Pty Ltd | Raspberry Pi licence (LICENSE.RP) | all images |
| [CYW43439 radio firmware](#cyw43439-radio-firmware) | Infineon Technologies (Cypress) | binary, shipped in cyw43-driver | all images |
| [lwIP](#lwip) | Swedish Institute of Computer Science | BSD-3-Clause | all images |
| [BearSSL](#bearssl) | Thomas Pornin | MIT | all images |
| [LittleFS](#littlefs) | Arm Limited and the littlefs authors | BSD-3-Clause | all images |
| [MicroPython DHCP server](#micropython-dhcp-server) | Damien P. George | MIT | all images |
| [LEAmDNS](#leamdns) | LaborEtArs | MIT | pico_w_release, pico_w_asserts, pico2_w_release |
| [newlib](#newlib) | Red Hat, Inc. and others | BSD-style (COPYING.NEWLIB) | all images |
| [GCC runtime libraries](#gcc-runtime-libraries) | Free Software Foundation, Inc. | GPL-3.0 with the GCC Runtime Library Exception | all images |
| [Adafruit GFX Library](#adafruit-gfx-library) | Adafruit Industries | BSD-2-Clause | the five images with the touch panel |
| [Adafruit ILI9341](#adafruit-ili9341) | Adafruit Industries (Limor Fried) | BSD | the five images with the touch panel |
| [XPT2046_Touchscreen](#xpt2046_touchscreen) | Paul Stoffregen | MIT | the five images with the touch panel |
| [PubSubClient](#pubsubclient) | Nicholas O'Leary | MIT | all images |
| [OneWirePIO_RP2040](#onewirepio_rp2040) | Ângelo Moisés Alves | MIT | all images |
| [DHT22PIO_RP2040](#dht22pio_rp2040) | Ângelo Moisés Alves | MIT | all images |
| [BuzzerPIO_RP2040](#buzzerpio_rp2040) | Ângelo Moisés Alves | MIT | all images but pico_w_air |
| [TwoWirePIO_RP2040](#twowirepio_rp2040) | Ângelo Moisés Alves | MIT | all images |
| [BMx280PIO_RP2040](#bmx280pio_rp2040) | Ângelo Moisés Alves | MIT | all images |
| [BTstack](#btstack) | BlueKitchen GmbH | Raspberry Pi licence (LICENSE.RP) | pico_w_alpha, pico_w_air |
| [GNU FreeFont (FreeSans Bold)](#gnu-freefont-freesans-bold) | GNU FreeFont contributors | GPL-3.0-or-later with the font exception | the five images with the touch panel |
| [Liberation Sans Bold](#liberation-sans-bold) | Red Hat, Inc. (digitized data: Google, Arimo) | SIL Open Font License 1.1 | all images |
| [SHA-256 for JavaScript](#sha-256-for-javascript) | Geraint Luff | public domain | all images |

## Arduino-Pico core

The Arduino core for the RP2040 and RP2350, with its WiFi, WebServer, HTTPClient, DNSServer, Updater, PicoOTA, SPI and Wire libraries. Its networking and filesystem code comes from the ESP8266 Arduino core, under the same licence.

- Copyright holder: Earle F. Philhower, III
- Licence: LGPL-2.1, text in [LICENSES/arduino-pico-LGPL-2.1.txt](LICENSES/arduino-pico-LGPL-2.1.txt)
- Images: all images
- Source: <https://github.com/earlephilhower/arduino-pico>

SIMUT publishes its complete source, so an image can be rebuilt against a modified core, as the LGPL requires.

## Raspberry Pi Pico SDK

Hardware access, multicore, flash and boot code for the RP2040 and RP2350.

- Copyright holder: Raspberry Pi (Trading) Ltd.
- Licence: BSD-3-Clause, text in [LICENSES/pico-sdk-BSD-3-Clause.txt](LICENSES/pico-sdk-BSD-3-Clause.txt)
- Images: all images
- Source: <https://github.com/raspberrypi/pico-sdk>

## TinyUSB

The USB stack behind the USB serial console.

- Copyright holder: Ha Thach (tinyusb.org)
- Licence: MIT, text in [LICENSES/tinyusb-MIT.txt](LICENSES/tinyusb-MIT.txt)
- Images: all images
- Source: <https://github.com/hathach/tinyusb>

## cyw43-driver

Driver for the Infineon CYW43439 Wi-Fi and Bluetooth chip of the Pico W and the Pico 2 W.

- Copyright holder: George Robotics Pty Ltd
- Licence: Raspberry Pi licence (LICENSE.RP), text in [LICENSES/cyw43-driver-LICENSE.RP.txt](LICENSES/cyw43-driver-LICENSE.RP.txt)
- Images: all images
- Source: <https://github.com/georgerobotics/cyw43-driver>

Licensed by Raspberry Pi Ltd for use with its own chips only. The driver's other licence (its LICENSE file) is for non-commercial use; LICENSE.RP is the one that covers SIMUT, which runs on the Pico W and the Pico 2 W.

## CYW43439 radio firmware

The firmware the radio chip loads at boot: Wi-Fi in every image, Bluetooth too in the alpha and Air images.

- Copyright holder: Infineon Technologies (Cypress)
- Licence: binary, shipped in cyw43-driver, text in [LICENSES/cyw43-driver-LICENSE.RP.txt](LICENSES/cyw43-driver-LICENSE.RP.txt)
- Images: all images
- Source: <https://github.com/georgerobotics/cyw43-driver/tree/main/firmware>

Shipped inside cyw43-driver; the Pico SDK carries no separate licence for it, so it travels under the driver's terms above.

## lwIP

The TCP/IP stack (Adam Dunkels and the lwIP contributors), bundled in the Arduino-Pico core.

- Copyright holder: Swedish Institute of Computer Science
- Licence: BSD-3-Clause, text in [LICENSES/lwip-BSD-3-Clause.txt](LICENSES/lwip-BSD-3-Clause.txt)
- Images: all images
- Source: <https://savannah.nongnu.org/projects/lwip/>

## BearSSL

TLS for HTTPS and MQTTS, SHA-256, and the P-256 check of signed updates.

- Copyright holder: Thomas Pornin
- Licence: MIT, text in [LICENSES/bearssl-MIT.txt](LICENSES/bearssl-MIT.txt)
- Images: all images
- Source: <https://bearssl.org>

## LittleFS

The flash filesystem: configuration, history, language packs.

- Copyright holder: Arm Limited and the littlefs authors
- Licence: BSD-3-Clause, text in [LICENSES/littlefs-BSD-3-Clause.txt](LICENSES/littlefs-BSD-3-Clause.txt)
- Images: all images
- Source: <https://github.com/littlefs-project/littlefs>

## MicroPython DHCP server

Hands out addresses on the setup access point; part of the Arduino-Pico WiFi library.

- Copyright holder: Damien P. George
- Licence: MIT, text in [LICENSES/micropython-dhcpserver-MIT.txt](LICENSES/micropython-dhcpserver-MIT.txt)
- Images: all images
- Source: <https://micropython.org>

## LEAmDNS

The mDNS responder that answers for the device name on the local network; part of the Arduino-Pico core.

- Copyright holder: LaborEtArs
- Licence: MIT, text in [LICENSES/leamdns-MIT.txt](LICENSES/leamdns-MIT.txt)
- Images: pico_w_release, pico_w_asserts, pico2_w_release
- Source: <https://github.com/LaborEtArs/ESP8266mDNS>

## newlib

The C library of the toolchain: memory allocation, strings, formatted output.

- Copyright holder: Red Hat, Inc. and others
- Licence: BSD-style (COPYING.NEWLIB), text in [LICENSES/newlib-COPYING.NEWLIB.txt](LICENSES/newlib-COPYING.NEWLIB.txt)
- Images: all images
- Source: <https://sourceware.org/newlib/>

## GCC runtime libraries

libgcc and libstdc++, linked by the compiler. The exception lets them be combined with code under any licence.

- Copyright holder: Free Software Foundation, Inc.
- Licence: GPL-3.0 with the GCC Runtime Library Exception, text in [LICENSES/gcc-runtime-exception-3.1.txt](LICENSES/gcc-runtime-exception-3.1.txt), [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt)
- Images: all images
- Source: <https://gcc.gnu.org>

## Adafruit GFX Library

Drawing primitives for the touch panel, and the classic 5x7 font of its text screens.

- Copyright holder: Adafruit Industries
- Licence: BSD-2-Clause, text in [LICENSES/adafruit-gfx-BSD-2-Clause.txt](LICENSES/adafruit-gfx-BSD-2-Clause.txt)
- Images: the five images with the touch panel
- Source: <https://github.com/adafruit/Adafruit-GFX-Library>

## Adafruit ILI9341

Driver for the ILI9341 controller of the touch panel.

- Copyright holder: Adafruit Industries (Limor Fried)
- Licence: BSD, text in [LICENSES/adafruit-ili9341-BSD.txt](LICENSES/adafruit-ili9341-BSD.txt)
- Images: the five images with the touch panel
- Source: <https://github.com/adafruit/Adafruit_ILI9341>

Its source files say "BSD license, all text here must be included in any redistribution", so the whole header is reproduced. Its README says MIT; the source is what this list follows.

## XPT2046_Touchscreen

Driver for the XPT2046 touch controller.

- Copyright holder: Paul Stoffregen
- Licence: MIT, text in [LICENSES/xpt2046-touchscreen-MIT.txt](LICENSES/xpt2046-touchscreen-MIT.txt)
- Images: the five images with the touch panel
- Source: <https://github.com/PaulStoffregen/XPT2046_Touchscreen>

## PubSubClient

The MQTT client behind MQTT and MQTTS telemetry.

- Copyright holder: Nicholas O'Leary
- Licence: MIT, text in [LICENSES/pubsubclient-MIT.txt](LICENSES/pubsubclient-MIT.txt)
- Images: all images
- Source: <https://github.com/knolleary/pubsubclient>

## OneWirePIO_RP2040

1-Wire on the PIO, for the DS18B20 probes.

- Copyright holder: Ângelo Moisés Alves
- Licence: MIT, text in [LICENSES/OneWirePIO_RP2040-MIT.txt](LICENSES/OneWirePIO_RP2040-MIT.txt)
- Images: all images
- Source: <https://github.com/angeloINTJ/OneWirePIO_RP2040>

## DHT22PIO_RP2040

The DHT22 protocol on the PIO.

- Copyright holder: Ângelo Moisés Alves
- Licence: MIT, text in [LICENSES/DHT22PIO_RP2040-MIT.txt](LICENSES/DHT22PIO_RP2040-MIT.txt)
- Images: all images
- Source: <https://github.com/angeloINTJ/DHT22PIO_RP2040>

## BuzzerPIO_RP2040

Tones and melodies for the buzzer on the PIO.

- Copyright holder: Ângelo Moisés Alves
- Licence: MIT, text in [LICENSES/BuzzerPIO_RP2040-MIT.txt](LICENSES/BuzzerPIO_RP2040-MIT.txt)
- Images: all images but pico_w_air
- Source: <https://github.com/angeloINTJ/BuzzerPIO_RP2040>

## TwoWirePIO_RP2040

I2C on the PIO (the WirePIO library).

- Copyright holder: Ângelo Moisés Alves
- Licence: MIT, text in [LICENSES/TwoWirePIO_RP2040-MIT.txt](LICENSES/TwoWirePIO_RP2040-MIT.txt)
- Images: all images
- Source: <https://github.com/angeloINTJ/TwoWirePIO_RP2040>

## BMx280PIO_RP2040

BME280 and BMP280 driver.

- Copyright holder: Ângelo Moisés Alves
- Licence: MIT, text in [LICENSES/BMx280PIO_RP2040-MIT.txt](LICENSES/BMx280PIO_RP2040-MIT.txt)
- Images: all images
- Source: <https://github.com/angeloINTJ/BMx280PIO_RP2040>

## BTstack

The Bluetooth stack behind the Bluetooth console of the alpha and Air images.

- Copyright holder: BlueKitchen GmbH
- Licence: Raspberry Pi licence (LICENSE.RP), text in [LICENSES/btstack-LICENSE.RP.txt](LICENSES/btstack-LICENSE.RP.txt)
- Images: pico_w_alpha, pico_w_air
- Source: <https://github.com/bluekitchen/btstack>

Raspberry Pi Ltd licenses BTstack to the buyers of the Pico W and the Pico 2 W for use in the products they build on them. BTstack's own licence (non-commercial) does not apply to that use.

## GNU FreeFont (FreeSans Bold)

The 9, 12 and 24 pt faces of the touch panel, converted to bitmaps and cut down to the glyphs SIMUT draws.

- Copyright holder: GNU FreeFont contributors
- Licence: GPL-3.0-or-later with the font exception, text in [LICENSES/freefont-GPL-3.0-font-exception.txt](LICENSES/freefont-GPL-3.0-font-exception.txt), [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt)
- Images: the five images with the touch panel
- Source: <https://www.gnu.org/software/freefont/>

## Liberation Sans Bold

Five glyph outlines traced into the SIMUT wordmark of the web pages. No font file is embedded.

- Copyright holder: Red Hat, Inc. (digitized data: Google, Arimo)
- Licence: SIL Open Font License 1.1, text in [LICENSES/liberation-fonts-OFL-1.1.txt](LICENSES/liberation-fonts-OFL-1.1.txt)
- Images: all images
- Source: <https://github.com/liberationfonts/liberation-fonts>

## SHA-256 for JavaScript

Hashes the password in the login page before it leaves the browser.

- Copyright holder: Geraint Luff
- Licence: public domain, text in [LICENSES/sha256-js-public-domain.txt](LICENSES/sha256-js-public-domain.txt)
- Images: all images
- Source: <https://github.com/geraintluff/sha256>

## Compiled, but not in any image

Built along the way and dropped by the linker; nothing of them reaches the device.

- **Adafruit BusIO**: compiled as a dependency of Adafruit GFX Library; no symbol survives the link.
- **http-parser**: compiled with the Arduino-Pico WebServer library; no symbol survives the link.
- **Adafruit STMPE610, Adafruit TouchScreen, Adafruit TSC2007, Adafruit SH110X**: downloaded as declared dependencies of Adafruit ILI9341; never compiled.
