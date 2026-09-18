FROM python:3.11-slim

# gcc/g++/make: native test envs compile on the host CPU
# git: lib_deps includes git-pinned libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc \
        g++ \
        make \
        git \
    && rm -rf /var/lib/apt/lists/*

# Pin PlatformIO Core to a known-good version so container builds are
# reproducible (platformio.ini does not pin a Core version)
# zopfli is optional for the build (tools/build_webui_gz.py falls back to gzip -9
# without it) but the flash budgets are measured with it, so the container has it.
RUN pip install --no-cache-dir platformio==6.1.19 zopfli

# Pre-cache the Pico W platform + ARM toolchain into the image layer.
# Git ref mirrors the #bd6fb6a pin in platformio.ini — whenever that pin
# changes, update this one together with it.
RUN pio pkg install --global \
        --platform "https://github.com/maxgerhardt/platform-raspberrypi.git#bd6fb6a"

WORKDIR /workspace
