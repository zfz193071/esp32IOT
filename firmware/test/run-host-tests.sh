#!/bin/sh
set -eu

with_preview=false
with_sockets=false
for option in "$@"; do
    case "$option" in
        --with-preview) with_preview=true ;;
        --with-sockets) with_sockets=true ;;
        *) echo 'Usage: sh firmware/test/run-host-tests.sh [--with-preview] [--with-sockets]' >&2; exit 2 ;;
    esac
done

firmware_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/esp32fan-test.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" -I "$firmware_dir/test/arduino-stub" \
    "$firmware_dir/src/board/Esp32BootId.cpp" \
    "$firmware_dir/src/adapters/StateApi.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/test/Esp32BootIdTest.cpp" \
    -o "$build_dir/esp32-boot-id-test"
# Separate processes model fresh boot lifetime; no test-only cache reset API.
"$build_dir/esp32-boot-id-test" first
"$build_dir/esp32-boot-id-test" second
"$build_dir/esp32-boot-id-test" zero

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/board/BoardProfile.cpp" \
    "$firmware_dir/test/BoardProfileTest.cpp" \
    -o "$build_dir/board-profile-test"
"$build_dir/board-profile-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/board/BoardProfile.cpp" \
    "$firmware_dir/src/board/BoardIoAdapter.cpp" \
    "$firmware_dir/test/BoardIoAdapterTest.cpp" \
    -o "$build_dir/board-io-adapter-test"
"$build_dir/board-io-adapter-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/board/BoardProfile.cpp" \
    "$firmware_dir/src/board/ButtonLedExperiment.cpp" \
    "$firmware_dir/src/input/ButtonDebouncer.cpp" \
    "$firmware_dir/test/ButtonLedExperimentTest.cpp" \
    -o "$build_dir/button-led-experiment-test"
"$build_dir/button-led-experiment-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/test/arduino-stub" -I "$firmware_dir/src" \
    "$firmware_dir/src/board/ArduinoGpioPort.cpp" \
    "$firmware_dir/test/ArduinoGpioPortTest.cpp" \
    -o "$build_dir/arduino-gpio-port-test"
"$build_dir/arduino-gpio-port-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/test/arduino-stub" -I "$firmware_dir/src" \
    "$firmware_dir/src/board/ArduinoNetworkPort.cpp" \
    "$firmware_dir/test/ArduinoNetworkPortTest.cpp" \
    -o "$build_dir/arduino-network-port-test"
"$build_dir/arduino-network-port-test"

# Compile the real BSD adapter even when localhost listeners are unavailable.
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" -c "$firmware_dir/src/board/SocketHttpTransport.cpp" \
    -o "$build_dir/socket-http-transport.o"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror -pthread \
    -I "$firmware_dir/src" -I "$firmware_dir/test/rtos-stub" \
    "$firmware_dir/test/Esp32SocketCloserTest.cpp" \
    -o "$build_dir/esp32-socket-closer-test"
"$build_dir/esp32-socket-closer-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror -pthread \
    -I "$firmware_dir/src" -I "$firmware_dir/test/rtos-stub" -I "$firmware_dir/test/arduino-stub" \
    "$firmware_dir/src/board/Esp32SerialSink.cpp" \
    "$firmware_dir/src/adapters/TextDiagnostics.cpp" \
    "$firmware_dir/test/Esp32SerialSinkTest.cpp" \
    -o "$build_dir/esp32-serial-sink-test"
"$build_dir/esp32-serial-sink-test"

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
    "$firmware_dir/src/config/ConfigRecordCodec.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/ConfigRecordCodecTest.cpp" \
    -o "$build_dir/config-record-codec-test"
"$build_dir/config-record-codec-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/test/arduino-stub" -I "$firmware_dir/src" \
    "$firmware_dir/src/board/NvsConfigPersistence.cpp" \
    "$firmware_dir/src/config/ConfigRecordCodec.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/NvsConfigPersistenceTest.cpp" \
    -o "$build_dir/nvs-config-persistence-test"
"$build_dir/nvs-config-persistence-test"

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
    "$firmware_dir/src/adapters/HttpRequestParser.cpp" \
    "$firmware_dir/src/adapters/WriteApi.cpp" \
    "$firmware_dir/src/adapters/StateApi.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/HttpRequestParserTest.cpp" \
    -o "$build_dir/http-request-parser-test"
"$build_dir/http-request-parser-test"

node "$firmware_dir/tools/embed-web.mjs" "$build_dir/generated"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti \
    -I "$firmware_dir/src" -I "$build_dir/generated" \
    "$build_dir/generated/WebAssets.cpp" \
    "$firmware_dir/src/adapters/HttpApiPort.cpp" \
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
    "$firmware_dir/test/HttpApiPortTest.cpp" \
    -o "$build_dir/http-api-port-test"
"$build_dir/http-api-port-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti \
    -I "$firmware_dir/src" -I "$build_dir/generated" \
    "$build_dir/generated/WebAssets.cpp" \
    "$firmware_dir/src/adapters/NetworkHttpApi.cpp" \
    "$firmware_dir/src/adapters/HttpApiPort.cpp" \
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
    "$firmware_dir/test/NetworkHttpApiTest.cpp" \
    -o "$build_dir/network-http-api-test"
"$build_dir/network-http-api-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/services/TemperatureService.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/TemperatureServiceTest.cpp" \
    -o "$build_dir/temperature-service-test"
"$build_dir/temperature-service-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/services/NetworkService.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/NetworkServiceTest.cpp" \
    -o "$build_dir/network-service-test"
"$build_dir/network-service-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/app/AppLoop.cpp" \
    "$firmware_dir/src/input/InfraredInput.cpp" \
    "$firmware_dir/src/services/NetworkService.cpp" \
    "$firmware_dir/src/services/TemperatureService.cpp" \
    "$firmware_dir/src/input/ButtonDebouncer.cpp" \
    "$firmware_dir/src/adapters/StateApi.cpp" \
    "$firmware_dir/src/adapters/WriteApi.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/AppLoopTest.cpp" \
    -o "$build_dir/app-loop-test"
"$build_dir/app-loop-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/input/InfraredInput.cpp" \
    "$firmware_dir/test/InfraredInputTest.cpp" \
    -o "$build_dir/infrared-input-test"
"$build_dir/infrared-input-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/app/AppLoop.cpp" \
    "$firmware_dir/src/input/InfraredInput.cpp" \
    "$firmware_dir/src/services/NetworkService.cpp" \
    "$firmware_dir/src/services/TemperatureService.cpp" \
    "$firmware_dir/src/input/ButtonDebouncer.cpp" \
    "$firmware_dir/src/adapters/StateApi.cpp" \
    "$firmware_dir/src/adapters/WriteApi.cpp" \
    "$firmware_dir/src/config/ConfigStore.cpp" \
    "$firmware_dir/src/domain/FanController.cpp" \
    "$firmware_dir/test/AppLoopPressureTest.cpp" \
    -o "$build_dir/app-loop-pressure-test"
"$build_dir/app-loop-pressure-test"

"${CXX:-c++}" -std=c++11 -Wall -Wextra -Wpedantic -Werror \
    -I "$firmware_dir/src" \
    "$firmware_dir/src/adapters/TextDiagnostics.cpp" \
    "$firmware_dir/test/TextDiagnosticsTest.cpp" \
    -o "$build_dir/text-diagnostics-test"
"$build_dir/text-diagnostics-test"

node --test "$firmware_dir/test/WebClientTest.mjs" "$firmware_dir/test/ConfigDraftTest.mjs" "$firmware_dir/test/WebAssetsTest.mjs" "$firmware_dir/test/StateApiTest.mjs" "$firmware_dir/test/ReleaseReadinessTest.mjs" "$firmware_dir/test/CommandJsonTest.mjs" "$firmware_dir/test/BoardInspectionTest.mjs"

if [ "$with_preview" = true ]; then
    node --test "$firmware_dir/test/PreviewApiTest.mjs"
else
    echo 'SKIP: localhost preview HTTP tests (use --with-preview to include)'
fi
if [ "$with_sockets" = true ]; then
    sh "$firmware_dir/test/run-socket-tests.sh"
else
    echo 'SKIP: real C++ localhost TCP tests (use --with-sockets to include)'
fi
echo 'PASS: requested host regression suites (no ESP32 hardware validation)'
