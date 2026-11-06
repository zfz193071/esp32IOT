# ESP32 桌面智能风扇

[简体中文](README.zh-CN.md) | [English](README.md)

以已购买的 ESP32 升级版学习套件为基础，逐步实现按键、红外遥控、局域网网页控制和温度自动控制。

当前阶段：已取得并核对一批厂家资料，仍待实际板卡版本、电机／电源参数与接线核验；已实现主循环软件装配、BoardProfile 候选配置校验、Arduino GPIO／Wi-Fi 适配、手动控制器、按键消抖、红外映射与重复帧过滤、手机控制页面、C++ 状态响应与写处理、请求协调、网页温控配置、配置字节编解码、NVS 存储适配及保存结果未知处理、采集／网络调度与自动控制规则，通过主机或模拟测试。固定工具链下的通用 ESP32 参考目标及四个独立诊断程序已编译／链接通过；尚未完成完整板端固件、Flash 实物验证、接线验证、已确认实物板型的编译或烧录。

正式运行时，C++ 固件在 ESP32 上读取输入并控制电机；ESP32 同时向同一局域网的手机或电脑浏览器提供网页与 API，页面 JavaScript 在浏览器里运行。电脑上的 Node.js 目前只用于测试、生成网页资源和启动本机模拟预览，实际设备运行不依赖电脑持续开机。当前尚无可烧录的完整固件，所以本机预览不能控制实物。

## 设计入口

- [系统架构与开发边界](docs/architecture.md)
- [交互式架构图](.archify/architecture-esp32-fan-20261028-105448/esp32-fan.html)
- [硬件核对清单](docs/hardware.md)
- [厂家资料核对与可开发范围](docs/hardware-material-review.md)
- [六张图片核对与工单开发范围](docs/hardware-image-review.md)
- [全部工单与执行顺序](docs/issues/README.md)：25 张必需工单、7 张后续扩展规划单。
- [手动控制器开发与测试](docs/development/domain.md)：运行 `sh firmware/test/run-host-tests.sh`，无需连接硬件。
- [按键消抖开发与测试](docs/development/button-input.md)：同一测试入口，覆盖长按、抖动、启动按住与计时回绕；独立 ButtonLedBringup 已通过参考编译。
- [手机控制页面与模拟预览](docs/development/web-control.md)：运行 `node firmware/tools/preview-server.mjs`，打开 `http://127.0.0.1:8312`；仅模拟设备，不控制实物。
- [温控配置与存储边界](docs/development/config-store.md)：校验、故障回退、写入失败保护及板端接入要求。
- [自动温控与故障恢复](docs/development/automatic-control.md)：回差、温度过期、手动接管与主循环调用顺序。
- [网页温控配置与模拟接口](docs/development/web-temperature-config.md)：草稿保护、保存确认与存储失败模拟。
- [网页编入 C++ 固件](docs/development/web-assets.md)：资源生成、路由表与字节一致性验证。
- [真实 ESP32 联调流程](docs/development/esp32-bringup.md)：从 USB 烧录、串口日志到实物风扇联调。
- [串口最小程序与 Arduino GPIO 端口](docs/development/serial-bringup.md)：不驱动电机的最小程序及板端 GPIO 接入边界。
- [从零运行与排障指南](docs/development/from-zero.md)：当前可执行命令、实物接入顺序与故障定位。
- [C++ 状态响应与 HTTP 增量解析](docs/development/state-api.md)：状态 JSON、分段接收、请求大小预算、超时及响应头要求。
- [HTTP 连接与有界发送](docs/development/http-connection.md)：网络变化驱动监听启停、BSD socket、网页分段发送与 ESP32 关闭任务；重连回环 TCP 已验证，待设备联调。
- [Arduino-ESP32 参考目标编译](docs/development/arduino-reference-build.md)：隔离工具链、固定版本、网页生成与编译检查入口；实际板型和烧录参数待确认。
- [C++ 写请求与控制器接入](docs/development/write-api.md)：严格 JSON、同源校验与配置保存。
- [非阻塞温度采集调度](docs/development/temperature-service.md)：驱动契约、转换超时、采样健康与主循环顺序。
- [Wi-Fi 连接与限频重试](docs/development/network-service.md)：连接超时、IP 事件、私有配置与断网行为。
- [主循环装配与最终输出](docs/development/app-loop.md)：启动关闭、有限 API 处理与一致输出。
- [红外输入与重复帧过滤](docs/development/infrared-input.md)：精确映射、长按过滤、时间戳与板端解码契约。
- [软件回归覆盖与实物验证边界](docs/development/regression.md)：统一测试入口、可选 HTTP 模拟测试及手工验收步骤。
- [输入压力与连续运行验收](docs/development/pressure-validation.md)：主机调度压力测试与实物记录模板。
- [首版交付准备清单](docs/delivery/handoff.md)：已有软件证据、发布门槛与待补的固件记录。

第一版 MVP 到 M4：手机在同一 Wi-Fi 下打开 ESP32 提供的网页，控制小风扇，并同步按键、红外操作后的控制状态。M5 增加温控，M6 完成异常场景验收。

本项目当前控制的是套件中的低压直流电机及扇叶，不涉及市电家电改装。电机驱动和供电要求必须先完成硬件核对。
