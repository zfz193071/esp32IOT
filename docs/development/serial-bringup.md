# ESP-004 串口最小程序与 Arduino GPIO 端口

`firmware/arduino/SerialBringup/SerialBringup.ino` 是独立的 Arduino-ESP32 入门程序：启动串口为 115200，打印固定标识、芯片型号、Flash 容量和“Motor GPIO: unconfigured”。它不设置任何 GPIO，不读 Wi-Fi 凭据，也不驱动电机。目的是在实物板型核实后先证明工具链、串口与烧录路径可工作。当前仓库尚未锁定实际 USB-C 核心板型号或安装 Arduino-ESP32 工具链，因此**尚未声称编译或烧录通过**。

资料中的 Arduino 攻略以 Micro-USB、CH340 核心板和 `ESP32 Dev Module` 为例；实际照片中的核心板为 USB-C。选板、Flash 配置、串口及驱动必须按实物确认，不能仅凭教程照搬。实物操作前保持电机断开。确认后在 Arduino IDE 打开该 `.ino`，选择匹配的板型与串口，编译并上传；串口监视器设为 115200，记录完整输出和烧录工具版本。没有串口时先核对数据线、USB 芯片、系统端口与板卡下载模式。

`firmware/src/board/ArduinoGpioPort.*` 提供后续设备装配用的 `GpioPort` 实现。输出初始化先通过 ESP-IDF `gpio_set_level` 预置关闭电平，再把引脚切换为输出；输入可选择内部上拉，日常读写使用 Arduino GPIO API。使用 IDF 接口是因为[新版 Arduino-ESP32 GPIO 实现](https://github.com/espressif/arduino-esp32/blob/3.3.3/cores/esp32/esp32-hal-gpio.c)的 `digitalWrite` 要求引脚已配置为 GPIO，不能依赖它在 `pinMode` 前预置电平；[2.0.4 的实现](https://github.com/espressif/arduino-esp32/blob/2.0.4/cores/esp32/esp32-hal-gpio.c)则直接调用 `gpio_set_level`。主机桩测试验证调用顺序和电平映射，不能证明实物复位／烧录期间没有瞬态，也不能代替电机驱动和供电核验。完整业务固件仍需实际 BoardProfile、启动装配、网络／HTTP、温度、红外和非易失存储适配。

从仓库根目录执行 `sh firmware/test/run-host-tests.sh` 可运行该端口的主机桩测试。`firmware/test/arduino-stub/Arduino.h` 仅供主机测试，不参与目标板编译。
