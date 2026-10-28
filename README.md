# ESP32 桌面智能风扇

以已购买的 ESP32 升级版学习套件为基础，逐步实现按键、红外遥控、局域网网页控制和温度自动控制。

当前阶段：硬件核对等待实物确认与商家资料；已实现手动控制器、按键消抖、手机控制页面、请求协调、温控配置与自动控制规则，通过主机或模拟测试。尚未完成板端固件接入、持久化适配、接线验证或设备烧录。

## 设计入口

- [系统架构与开发边界](docs/architecture.md)
- [交互式架构图](.archify/architecture-esp32-fan-20261028-105448/esp32-fan.html)
- [硬件核对清单](docs/hardware.md)
- [六张图片核对与工单开发范围](docs/hardware-image-review.md)
- [全部工单与执行顺序](docs/issues/README.md)：25 张必需工单、7 张后续扩展规划单。
- [手动控制器开发与测试](docs/development/domain.md)：运行 `sh firmware/test/run-host-tests.sh`，无需连接硬件。
- [按键消抖开发与测试](docs/development/button-input.md)：同一测试入口，覆盖长按、抖动、启动按住与计时回绕。
- [手机控制页面与模拟预览](docs/development/web-control.md)：运行 `node firmware/tools/preview-server.mjs`，打开 `http://127.0.0.1:8312`；仅模拟设备，不控制实物。
- [温控配置与存储边界](docs/development/config-store.md)：校验、故障回退、写入失败保护及板端接入要求。

- [自动温控与故障恢复](docs/development/automatic-control.md)：回差、温度过期、手动接管与主循环调用顺序。

第一版 MVP 到 M4：手机在同一 Wi-Fi 下打开 ESP32 提供的网页，控制小风扇，并同步按键、红外操作后的控制状态。M5 增加温控，M6 完成异常场景验收。

本项目当前控制的是套件中的低压直流电机及扇叶，不涉及市电家电改装。电机驱动和供电要求必须先完成硬件核对。
