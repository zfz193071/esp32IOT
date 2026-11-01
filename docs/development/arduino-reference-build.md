# Arduino-ESP32 参考目标编译

本入口用于检查生产 C++ 模块能否与真实 Arduino-ESP32、ESP-IDF 和 FreeRTOS 头文件及库一起编译／链接。通用目标 `esp32:esp32:esp32` 是**编译参考目标**，不是根据实物确认的选板或烧录配置；不配置电机 GPIO，不执行上传或串口探测。

## 固定版本与隔离目录

版本记录在 `firmware/config/arduino-build.json`：Arduino CLI 1.5.1、Arduino-ESP32 3.3.3。3.3.3 与现有 GPIO 实现核对所用的核心版本一致，作为初始兼容性基线；实际板卡确认后再决定正式目标配置。核心官方包声明的 SDK 依赖为 `idf-release_v5.5-f1a1df9b-v3`，其工具依赖由官方平台包索引解析。

CLI 归档及核心归档的 SHA-256 固定在配置中。脚本从 Arduino 官方发布下载 CLI，从乐鑫官方索引取得固定核心及其依赖；先校验缓存归档，再由 Arduino 安装器校验和安装。为避免内置下载器在本机的慢速传输，安装脚本以两个并行 curl 下载任务预取官方归档。核心包及其依赖的 GitHub 归档下载失败时，按官方说明重试 `dl.espressif.com/github_assets` 镜像，仍使用同一 SHA-256 校验；CLI 归档不使用该回退。不启用跳过校验选项。

工具链、下载缓存、Arduino 用户目录、编译缓存均位于被 Git 忽略的 `firmware/build/toolchain/`。脚本传入专用配置文件，并从 Arduino 子进程环境中移除覆盖配置的 `ARDUINO_*` 变量；不修改全局 Arduino IDE 配置。当前归档表支持 macOS／Linux 的 x64、arm64，未列出的主机平台会明确拒绝；各主机平台是否实际验证需看下方记录。

## 使用方法

需要 Node.js、curl、tar 和首次安装时访问官方软件下载站的网络权限。完整核心包含多个芯片架构的工具，需预留数 GB 空间。仓库根目录执行：

```sh
node firmware/tools/setup-arduino.mjs
node firmware/tools/build-arduino.mjs
# 独立串口诊断（只编译，不装配业务模块）：
node firmware/tools/build-arduino.mjs --sketch SerialBringup
```

安装入口会下载／安装依赖；构建入口不安装或升级工具。无参数或 `--sketch CompileCheck` 构建业务集成检查；`--sketch SerialBringup` 仅复制独立串口程序，输入清单的生产源码列表为空、网页清单为 null。其他入口名会被拒绝。两种入口均为参考目标，无串口探测或上传。CLI 或已安装核心版本不匹配时构建拒绝继续，先按提示修复隔离安装。不要并发运行多个安装过程。

每次构建在 `firmware/build/arduino-check/build-*/` 创建新的装配目录，包括：

- `libraries/Esp32Fan/`：从生产 `firmware/src` 复制的 `.h`／`.cpp`，按 Arduino 库的 src 递归编译方式组织。
- 库内 `WebAssets.*` 与 `web-manifest.json`：重新生成的五个网页资源，生成失败则构建停止。
- `CompileCheck/CompileCheck.ino`：仅用于链接验证的入口。
- `build-inputs.json`：版本、源码摘要、入口摘要及网页清单。
- `output/`：真实交叉编译器输出；构建成功后才写 `build-result.json`，记录参考目标和 `.bin` 摘要。

不复制主机测试桩或 `wifi.local.h`，不读取实际 Wi-Fi 密码。每次装配使用新目录，失败产物保留供排查；未输出 PASS 不能记为编译成功。固定版本和输入清单支持复现构建过程，不保证不同路径／时间下二进制逐字节相同。

## 编译入口的运行边界

CompileCheck 连接真实 AppLoop、NVS 配置层、ArduinoNetworkPort、NetworkHttpApi、BSD socket、FreeRTOS 关闭任务和 Esp32SerialSink 日志发送任务；所有生产源文件都被编译。输出端口为无 GPIO 实现，没有传感器或红外实物驱动。Wi-Fi 使用固定占位符，网络适配器会拒绝连接，不能通过替换此文件的密码就当作完整控制固件。

入口在串口通过 `ESP32_FAN_COMPILE_CHECK` 说明用途；状态 bootId 已改为启动早期从 ESP32 内部熵源生成的随机标识，在 ADC／I2S／RF 使用前释放熵源，再构造 AppLoop，见[启动标识说明](domain.md#esp32-启动标识生成)。如果手动运行它，NVS 会在启动时被读取，入口不会自动保存配置；Arduino 核心初始化 NVS 的行为仍见 [配置持久化说明](config-store.md)。本入口不提供上传命令；真实设备验证先确认板型／Flash，并使用 [串口最小程序](serial-bringup.md)，随后按已核实 BoardProfile 装配业务固件。

## 官方依据

- [Arduino CLI 配置及配置优先级](https://docs.arduino.cc/arduino-cli/configuration/)：专用数据／用户／缓存目录及环境变量处理。
- [Arduino 库目录规范](https://docs.arduino.cc/arduino-cli/library-specification/)：src 下递归编译，库根头文件参与依赖查找。
- [Arduino CLI 1.5.1 发布](https://github.com/arduino/arduino-cli/releases/tag/v1.5.1)、[Arduino-ESP32 3.3.3 发布](https://github.com/espressif/arduino-esp32/releases/tag/3.3.3)：固定版本来源。
- [Espressif GitHub 归档镜像](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-tools.html#github-assets-mirror)：官方镜像地址与 URL 改写规则。

## 验证记录

2026-11-01 启动标识更新：参考构建装配 23 个生产 C++ 文件并通过编译／链接。程序 1,200,523 字节、静态全局数据 50,140 字节；目录 `firmware/build/arduino-check/build-smRAEn/`，二进制 SHA-256 为 `b46cc2687ea55eae82bf28f3511a9167458940caf76011fd3f89344c16472ee7`。ELF 确认 `esp32BootId`、`bootloader_random_enable/disable` 和 `esp_fill_random` 已链接。主机 23 个 C++ 程序、26 项 Node 测试通过；实际重启、熵源时序仍未验证。

2026-11-01 UART 适配更新：CompileCheck 接入 Esp32SerialSink 后重新编译／链接通过，装配 22 个生产 C++ 文件，程序 1,199,191 字节、静态全局数据 50,092 字节。输出目录 `firmware/build/arduino-check/build-zpsz3s/`，二进制 SHA-256 为 `0eb0781679717f0c44b436315553b05853fb1c15285a32b164ee5f81c83251c8`；ELF 确认日志工作任务、启动和提交函数已链接。任务栈和队列在运行时申请，不包含在静态全局数据统计里；实际栈余量、UART 行为仍未实测。

2026-11-01，在 macOS x86_64 上执行两个入口，均退出 0。首次安装遇到 GitHub 超时，核心依赖通过缓存及官方镜像补齐；所有下载均校验 SHA-256。其他主机平台尚未验证。

- 通用 `esp32:esp32:esp32` 全新装配目录编译／链接通过，21 个生产 `.cpp` 均生成目标文件，五个网页资源共 20,841 字节。
- ELF 中确认 AppLoop、NVS、NetworkHttpApi、SocketHttpTransport 和 FreeRTOS 关闭任务的链接符号。
- 编译器报告程序占用 1,198,123 / 1,310,720 字节（91%），静态全局数据占用 50,068 / 327,680 字节（15%）。这是参考默认分区；不代表实际 Flash 容量或运行时堆／任务栈余量，也未为真实固件确认分区。
- 本机产物：`firmware/build/arduino-check/build-IRgBMx/`；二进制 SHA-256 为 `29684b4191aaec6c5ab625617cf7f52b697b1a143b498001557046bdd011918c`。产物与工具链被忽略，不提交仓库。
- `sh firmware/test/run-host-tests.sh` 退出 0：21 个 C++ 测试程序、26 项 Node 测试通过；本轮未重跑可选模拟 HTTP／真实回环 TCP 测试。

尚未编译已确认的实物板型、烧录、读取真实串口或验证 Wi-Fi／Flash／GPIO。ESP-004 的整单验收项仍保持未勾选。
