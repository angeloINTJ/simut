#!/bin/bash
# Build and upload the BMx280 test sketch
echo "=== Compiling ==="
arduino-cli compile --fqbn "rp2040:rp2040:rpipicow:flash=2097152_1048576" /home/angelo/Documentos/simut/test_bmx280 2>&1 | tail -5

echo "=== Upload ==="
arduino-cli upload -p /dev/ttyACM0 --fqbn "rp2040:rp2040:rpipicow:flash=2097152_1048576" /home/angelo/Documentos/simut/test_bmx280 2>&1
