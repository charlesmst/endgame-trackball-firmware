#!/bin/sh
cd "$(dirname "$0")/../zmk" || exit 1

CHARLESMST_PAW="$(pwd)/../../zmk-config-roBa-charybdis-esb/zmk-paw3395-driver"
VFX_LED="$(pwd)/../../zmk-config-roBa-charybdis-esb/zmk-vfx-rgbled-indicator"
MODULES="${CHARLESMST_PAW};${VFX_LED};$(pwd)/../lariska-config;$(pwd)/../zmk-esb-endpoint"

west build --pristine always -s app -b nice_nano_v2 -- \
    -DSHIELD=lariska \
    "-DZMK_EXTRA_MODULES=${MODULES}"

cp "$(pwd)/build/zephyr/zmk.uf2" /mnt/c/Users/charl/Downloads/lariska_endgame.uf2
echo "Copied to /mnt/c/Users/charl/Downloads/lariska_endgame.uf2"
