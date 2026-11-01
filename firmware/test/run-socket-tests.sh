#!/bin/sh
set -eu
firmware_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/esp32fan-socket.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
node "$firmware_dir/tools/embed-web.mjs" "$build_dir/generated"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -pthread \
    -I "$firmware_dir/src" -I "$build_dir/generated" \
    "$build_dir/generated/WebAssets.cpp" \
    "$firmware_dir/src/board/SocketHttpTransport.cpp" \
    "$firmware_dir/src/adapters/HttpApiPort.cpp" \
    "$firmware_dir/src/adapters/NetworkHttpApi.cpp" \
    "$firmware_dir/src/adapters/HttpRequestParser.cpp" \
    "$firmware_dir/src/app/AppLoop.cpp" \
    "$firmware_dir/src/input/InfraredInput.cpp" \
    "$firmware_dir/src/input/ButtonDebouncer.cpp" \
    "$firmware_dir/src/services/NetworkService.cpp" \
    "$firmware_dir/src/services/TemperatureService.cpp" \
    "$firmware_dir/src/adapters/WriteApi.cpp" \
    "$firmware_dir/src/adapters/StateApi.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/SocketHttpTransportTest.cpp" \
    -o "$build_dir/socket-http-transport-test"
"$build_dir/socket-http-transport-test"
