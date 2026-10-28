# ESP32 Smart Desktop Fan

[English](README.md) | [简体中文](README.zh-CN.md)

This project uses an ESP32 upgraded learning kit to build a desktop fan with button, infrared remote, local-network web, and automatic temperature control in stages.

Current status: Hardware identification is waiting for confirmation of the actual parts and vendor documentation. The host-tested or simulated software includes the main loop, manual controller, button debouncing, infrared mapping and repeat filtering, mobile control page, C++ state and write handlers, request coordination, web temperature settings, temperature and network scheduling, and automatic control rules. Board integration, persistent storage adapters, wiring verification, and flashing have not been completed.

In the final system, C++ firmware will run on the ESP32, read inputs, and control the motor. The ESP32 will also serve the page and API to a phone or computer browser on the same local network; the page's JavaScript runs in that browser. Node.js on the computer is currently used only for tests, web asset generation, and a local simulator. The device will not need the computer to stay on. There is no complete flashable firmware yet, so the local preview cannot control physical hardware.

## Project guides

The linked design, development, and issue documents are currently in Chinese.

- [System architecture and development boundaries](docs/architecture.md)
- [Interactive architecture diagram](.archify/architecture-esp32-fan-20261028-105448/esp32-fan.html)
- [Hardware identification checklist](docs/hardware.md)
- [Review of six photos and development scope](docs/hardware-image-review.md)
- [All issues and execution order](docs/issues/README.md): 25 required issues and 7 planned extensions.
- [Manual controller development and tests](docs/development/domain.md): run `sh firmware/test/run-host-tests.sh` without hardware.
- [Button debouncing development and tests](docs/development/button-input.md): the same test command covers long presses, bounce, a held button at startup, and clock wraparound.
- [Mobile control page and local preview](docs/development/web-control.md): run `node firmware/tools/preview-server.mjs` and open `http://127.0.0.1:8312`; this simulates a device and cannot control the real fan.
- [Temperature settings and storage boundaries](docs/development/config-store.md): validation, recovery from corrupt data, write-failure protection, and board integration requirements.
- [Automatic temperature control and fault recovery](docs/development/automatic-control.md): hysteresis, stale readings, manual takeover, and main-loop call order.
- [Web temperature settings and simulated API](docs/development/web-temperature-config.md): draft preservation, save confirmation, and simulated storage failure.
- [Embedding the web page in C++ firmware](docs/development/web-assets.md): asset generation, route table, and byte-for-byte checks.
- [ESP32 hardware bring-up](docs/development/esp32-bringup.md): USB flashing, serial logs, and physical fan integration.
- [Start-from-zero and troubleshooting guide](docs/development/from-zero.md): commands available now, hardware integration order, and fault diagnosis.
- [C++ state responses and web contract](docs/development/state-api.md): state JSON, null values, safe integers, and response headers.
- [C++ write requests and controller integration](docs/development/write-api.md): strict JSON, same-origin validation, and configuration saving.
- [Non-blocking temperature sampling](docs/development/temperature-service.md): driver contract, conversion timeout, sample health, and main-loop order.
- [Wi-Fi connection and rate-limited retries](docs/development/network-service.md): connection timeout, IP events, private settings, and behavior when offline.
- [Main-loop assembly and final outputs](docs/development/app-loop.md): output off at startup, bounded API processing, and consistent outputs.
- [Infrared input and repeat filtering](docs/development/infrared-input.md): exact code mapping, long-press filtering, timestamps, and board decoder contract.
- [Software regression coverage and hardware validation boundaries](docs/development/regression.md): unified test command, optional simulated HTTP tests, and manual acceptance steps.
- [Input pressure and continuous-run validation](docs/development/pressure-validation.md): host scheduling stress test and hardware record template.
- [First-release preparation checklist](docs/delivery/handoff.md): existing software evidence, release criteria, and missing firmware records.

The first MVP ends at milestone M4: a phone on the same Wi-Fi opens a page served by the ESP32, controls the small fan, and sees state changes made by the button or infrared remote. M5 adds temperature control; M6 completes fault-scenario acceptance.

The project is intended for the kit's low-voltage DC motor and fan blade, not for modifying a mains-powered appliance. Motor driving and power requirements must be verified during hardware identification first.
