FROM python:3.11-slim

# gcc/g++/make: native test envs compile on the host CPU
# git: lib_deps includes git-pinned libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc \
        g++ \
        make \
        git \
        curl \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir platformio

# Pre-cache the Pico W platform + ARM toolchain into the image layer
RUN pio pkg install --global \
        --platform "https://github.com/maxgerhardt/platform-raspberrypi.git"

WORKDIR /workspace
