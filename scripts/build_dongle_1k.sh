#!/bin/sh
cd "$(dirname "$0")/../zmk" || exit 1

BOARD=efogtech_dongle_1k
BOARD_LABEL=efogtech
for arg in "$@"; do
    case "$arg" in
        --holyiot) BOARD=holyiot_dongle_1k; BOARD_LABEL=holyiot ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

west build --pristine always -s ../dongle-1k-firmware -b "$BOARD" -- \
    "-DZEPHYR_EXTRA_MODULES=$(pwd)/../endgame-trackball-config"

OBJCOPY="$HOME/.local/opt/zephyr-sdk-0.16.8/arm-zephyr-eabi/bin/arm-zephyr-eabi-objcopy"
OUT="$(pwd)/../output/dongle-1k-${BOARD_LABEL}.hex"
"$OBJCOPY" -O ihex build/zephyr/zephyr.elf "$OUT"
echo "Hex: $OUT"
