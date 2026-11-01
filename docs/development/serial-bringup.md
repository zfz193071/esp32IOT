# ESP-004 板卡识别与串口最小程序

2026-11-01 的六张[主板近照](../hardware-image-review.md#2026-11-01-主板近照索引)可读 USB-C 核心板上的 CH340C、模组罩 `ESP-32`、底板 KF56-3A 和 U3 ULN2003A。Flash 容量没有可读标记，先用芯片 ROM 读取，无需为了识别容量先烧录程序。当前固定工具链已安装并完成[参考编译](arduino-reference-build.md)，但实物板型／Flash 和上传配置尚未最终锁定。

## 1. 不写 Flash 的识别入口

需要先运行 `node firmware/tools/setup-arduino.mjs` 安装隔离工具链。识别入口使用核心包声明的 esptool 5.1.0，当前支持 macOS `/dev/cu.*`、Linux `/dev/ttyUSB*`／`/dev/ttyACM*` 路径；未实现 Windows 入口。

仅列出系统串口名称，不打开设备：

```sh
node firmware/tools/inspect-board.mjs --list
```

USB 数据线应插到**核心板的 USB-C**。操作前电机和其他外接负载断开；板上现有程序及复位阶段输出未知。连接前后对比列表选出新端口，蓝牙串口不是 ESP32 的证据。CH340C 丝印也不等于电脑已正确枚举；没有新增端口时先检查数据线、接口与系统 USB 设备，不自动安装驱动。

将下面占位端口换成实际新增端口，预览完整参数：

```sh
node firmware/tools/inspect-board.mjs --port /dev/cu.REPLACE_WITH_ACTUAL_PORT --dry-run
```

实际读取前，使用照片中核心板上的 **BOOT／RST**，按住 BOOT，按下并松开 RST，再松开 BOOT，进入 ROM 下载模式。它会中断原程序运行。这一顺序对应经典 ESP32 的 GPIO0 启动模式；若不能通信，保留错误结果核对板卡线路，不短接猜测的引脚。[官方启动模式说明](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)

随后执行：

```sh
node firmware/tools/inspect-board.mjs --port /dev/cu.REPLACE_WITH_ACTUAL_PORT
```

脚本固定执行 `--chip auto --baud 115200 --before no-reset --after no-reset --no-stub --connect-attempts 3 flash-id`，仅允许显式指定的串口，不接受其他 esptool 子命令。它不写入／擦除 Flash，不上传 RAM stub，不自动选择分区或 GPIO；不运行硬复位序列，成功后留在下载模式。操作涉及串口和 ROM 通信，不是对设备完全无影响的文件读取。下一步操作前保持负载断开；不要直接把编译检查产物烧入板卡。

识别进程限时 45 秒，使用独立配置和清理过的 `ESPTOOL_*` 环境。原始 stdout／stderr、退出码、时间、端口和参数保存在被忽略的 `firmware/build/board-inspection/read-*/result.json`。失败返回非零，不从缺失字段猜型号或容量；成功也只保存原始结果，不自动修改工程板型。记录芯片型号、修订号、Flash manufacturer/device ID 和 detected size；完整模组订货型号可能仍无法由工具识别。

官方依据：[flash-id 输出](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html#read-spi-flash-id-flash-id)、[无复位与无 stub 参数](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/advanced-options.html)、[配置优先级](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/configuration-file.html)。参数同时与本机安装的 esptool 5.1.0 帮助核对。

## 2. 独立串口最小程序

`firmware/arduino/SerialBringup/SerialBringup.ino` 不设置电机／外设 GPIO，不读取 Wi-Fi 凭据，不包含业务控制器、网络服务或配置保存层。串口 115200，启动输出后每 5 秒重复，避免串口监视器晚打开时错过信息；不等待串口连接。

输出包括 `ESP32_FAN_SERIAL_BRINGUP v2`、芯片型号／修订号／核心数、由 JEDEC ID 推导的 Flash 字节数、Arduino／ESP-IDF 版本、`Motor GPIO: unconfigured` 和结束标记。修订号为 major × 100 + minor；这些信息不能检测底板型号、杜邦线或电气能力。程序没有主动保存配置，但 Arduino 核心初始化仍有自身 NVS 行为；它与前述 ROM 识别不是同一种操作。

只编译，不打开串口或上传：

```sh
node firmware/tools/build-arduino.mjs --sketch SerialBringup
```

仍使用通用参考目标。实际上传需先核对 ROM 识别结果与选板／Flash／分区参数；上传会替换已有固件，当前脚本不执行上传。烧录和串口真实输出成功后，才能记录设备验收。

## 3. Arduino GPIO 端口

`firmware/src/board/ArduinoGpioPort.*` 通过 ESP-IDF `gpio_set_level` 预置关闭电平，再设置输出模式；输入支持内部上拉。新版核心的 `digitalWrite` 要求引脚已经配置，不能依赖它在 `pinMode` 前预置电平，见 [Arduino-ESP32 3.3.3 实现](https://github.com/espressif/arduino-esp32/blob/3.3.3/cores/esp32/esp32-hal-gpio.c)。主机桩验证调用顺序，不代表复位／烧录阶段无瞬态。正式电机输出仍依赖已确认的 BoardProfile、负载、供电和复位关闭方案。

## 验证记录

2026-11-01，`SerialBringup` 在 macOS x86_64、Arduino CLI 1.5.1／Arduino-ESP32 3.3.3、通用 `esp32:esp32:esp32` 目标编译／链接通过：程序 279,031 字节，静态全局数据 20,792 字节。本机输出目录 `firmware/build/arduino-check/build-3cJRcY/`，二进制 SHA-256 为 `7dd698e7135389ed105c97abd7fa8374c4c6bce726e5578e13b8e085c70d7e84`。未烧录此程序。

本轮串口枚举仅见 `/dev/cu.BLTH` 和 `/dev/cu.Bluetooth-Incoming-Port`，未发现可认定为核心板的 USB 串口。已验证参数预览不打开端口；未执行 ROM 读取，不能记录真实 Flash 容量。
