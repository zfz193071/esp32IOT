#!/bin/sh
set -eu

firmware_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/esp32fan-test.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/test/FanControllerTest.cpp" \
    -o "$build_dir/fan-controller-test"
"$build_dir/fan-controller-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/input/ButtonDebouncer.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/test/ButtonDebouncerTest.cpp" \
    -o "$build_dir/button-debouncer-test"
"$build_dir/button-debouncer-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/ConfigStoreTest.cpp" \
    -o "$build_dir/config-store-test"
"$build_dir/config-store-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/AutomaticControlTest.cpp" \
    -o "$build_dir/automatic-control-test"
"$build_dir/automatic-control-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/adapters/WriteApi.cpp" \
    "$firmware_dir/src/adapters/StateApi.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/WriteApiTest.cpp" \
    -o "$build_dir/write-api-test"
"$build_dir/write-api-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/services/TemperatureService.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/TemperatureServiceTest.cpp" \
    -o "$build_dir/temperature-service-test"
"$build_dir/temperature-service-test"

node --test "$firmware_dir/test/WebClientTest.mjs" "$firmware_dir/test/ConfigDraftTest.mjs" "$firmware_dir/test/WebAssetsTest.mjs" "$firmware_dir/test/StateApiTest.mjs"
