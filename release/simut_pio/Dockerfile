FROM python:3.11-slim

# gcc/g++/make: native test envs compile on the host CPU
# git: lib_deps includes git-pinned libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc \
        g++ \
        make \
        git \
    && rm -rf /var/lib/apt/lists/*

# Pin PlatformIO Core to a known-good version (match with platformio.ini)
RUN pip install --no-cache-dir platformio==6.1.19

# Pre-cache the Pico W platform + ARM toolchain into the image layer.
# Git ref pinned to the same commit the default branch resolves to —
# matches what `platformio.ini` fetches when no ref is specified.
RUN pio pkg install --global \
        --platform "https://github.com/maxgerhardt/platform-raspberrypi.git#64c93ed89c4e300304715025dbdf239ed2b17b48"

WORKDIR /workspace
