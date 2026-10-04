#!/bin/sh
# Flash the prebuilt firmware onto an Arduino Mega 2560 (Linux / macOS).
# Usage: firmware/tools/flash.sh [serial_port] [hex_file]
#   serial_port  default: first port arduino-cli detects as a Mega (e.g. /dev/ttyACM0, /dev/cu.usbmodem*)
#   hex_file     default: firmware/PSN-Presepe/PSN-Presepe.ino.hex
# Needs arduino-cli (ARDUINO_CLI to override) with the arduino:avr core installed.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ACLI=${ARDUINO_CLI:-arduino-cli}
HEX=${2:-$HERE/../PSN-Presepe/PSN-Presepe.ino.hex}
PORT=$1
if [ -z "$PORT" ]; then
  PORT=$("$ACLI" board list | awk '/arduino:avr:mega/ {print $1; exit}')
fi
[ -n "$PORT" ] || { echo "No Arduino Mega found: pass the serial port as first argument"; exit 1; }
echo "Flashing $HEX -> $PORT"
"$ACLI" upload -p "$PORT" --fqbn arduino:avr:mega:cpu=atmega2560 --input-file "$HEX" --verify
