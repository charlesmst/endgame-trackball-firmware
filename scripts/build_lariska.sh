#!/bin/sh
cd "$(dirname "$0")/../zmk" || exit 1

CHARLESMST_PAW="$(pwd)/../../zmk-config-roBa-charybdis-esb/zmk-paw3395-driver"
MODULES="${CHARLESMST_PAW};$(pwd)/../lariska-config;$(pwd)/../zmk-esb-endpoint"

west build --pristine always -s app -b nice_nano_v2 -- \
    -DSHIELD=lariska \
    "-DZMK_EXTRA_MODULES=${MODULES}"
