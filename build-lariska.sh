#!/usr/bin/env bash
set -e

BASE="$(cd "$(dirname "$0")" && pwd)"
CHARLESMST_PAW="/home/charlesstein/personal/zmk-config-roBa-charybdis-esb/zmk-paw3395-driver"
MODULES="${CHARLESMST_PAW};${BASE}/lariska-config;${BASE}/zmk-esb-endpoint"

cd "${BASE}/zmk"
west build --pristine always -s app -b nice_nano_v2 -- \
    -DSHIELD=lariska \
    "-DZMK_EXTRA_MODULES=${MODULES}"

echo ""
echo "Built: ${BASE}/zmk/build/zephyr/zmk.uf2"
echo "Double-tap reset on the nice!nano, then copy zmk.uf2 to the NICENANO drive."
