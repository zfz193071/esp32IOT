# 控制规则与接口回归

ESP-022 的软件回归使用真实 C++ 控制器、配置层、调度层和 API 处理函数，并以模拟端口替代硬件。测试不需要 ESP32，不代表完成 HTTP 板端传输、Flash 持久化或实物验收。

## 执行入口

在仓库根目录运行：

```sh
sh firmware/test/run-host-tests.sh
```

需要支持 C++11 的主机编译器和支持 `node --test`、内置 fetch 的 Node.js。脚本使用 `CXX` 指定的编译器（默认 c++），开启严格告警并将告警视为错误，编译输出放在自动清理的临时目录。跨语言测试也会调用主机编译器。不下载依赖，不访问 ESP32。

需要同时检查 Node 模拟服务器的实际本地 HTTP 时运行：

```sh
sh firmware/test/run-host-tests.sh --with-preview
```

该选项额外运行 PreviewApiTest，启动并清理自己的模拟进程，使用本地端口 18313～18316；端口需空闲，运行环境需允许监听本地网络。不要为测试终止其他进程。默认执行会明确打印该项 SKIP，不能把默认成功记录为模拟 HTTP 已通过。

验证真实 C++ socket 驱动、网页发送和 AppLoop 联合路径：

```sh
sh firmware/test/run-socket-tests.sh
# 或默认套件加真实 TCP：
sh firmware/test/run-host-tests.sh --with-sockets
```

仅在 `127.0.0.1` 随机端口监听，与 Node 模拟服务无关。默认套件编译 BSD 驱动并运行 ESP32 关闭任务的主机线程／队列桩测试，TCP 测试单独明确 SKIP；两个可选参数可组合。TCP 测试仍不代表 ESP32 工具链和 Wi-Fi 验证。

任一断言或编译失败会使脚本非零退出；C++ 输出 `FAIL: 规则说明`，Node 输出失败用例和断言位置。只有所有请求的套件通过才打印最后的 PASS。退出码 2 表示不支持的脚本参数。

## 自动覆盖清单

| 规则 | 测试文件（firmware/test） | 验证边界 |
|---|---|---|
| 默认关闭、命令来源、显式开关和幂等 | FanControllerTest.cpp | 控制状态，不测 GPIO 电平 |
| 启动随机源顺序、标识编码／缓存与状态 API 契约 | Esp32BootIdTest.cpp | 三次独立进程注入样本；不验证实际熵质量或断电重启 |
| 按下消抖、启动按住、长按与计时回绕 | ButtonDebouncerTest.cpp | 归一化按键输入 |
| 阈值范围、间隔、损坏回退和保存失败 | ConfigStoreTest.cpp | 模拟原子存储，不测断电 Flash |
| NVS 句柄、固定长度读取及写入／提交未知结果 | NvsConfigPersistenceTest.cpp | 主机桩模拟旧／新值两种恢复，不测真实 Flash |
| 配置字节格式、CRC、截断／位损坏与恢复 | ConfigRecordCodecTest.cpp | 独立字节样本与模拟字节后端，不测真实 Flash |
| 回差等号、5000 ms 新鲜度、故障恢复和手动接管 | AutomaticControlTest.cpp | 包括边界两侧与计时回绕 |
| 严格参数、来源拒绝、保存失败 | WriteApiTest.cpp | 已解析请求，不测 TCP/HTTP 字节解析 |
| HTTP 分段／分帧、大小预算、超时和写接口串接 | HttpRequestParserTest.cpp | 原始内存字节，不测实际 TCP 和发送耗时 |
| HTTP 单连接、静态资源、短写、超时及主循环接入 | HttpApiPortTest.cpp | 真实生成网页与 AppLoop，模拟传输端口；不测 TCP 或板端 I/O 耗时 |
| HTTP 断网／换地址清理、重连、监听重试与同轮网络观察 | NetworkHttpApiTest.cpp | 真实 NetworkService／AppLoop、模拟监听器；不测真实无线连接 |
| ESP32 独立关闭任务、队列上限、申请失败及错误锁定 | Esp32SocketCloserTest.cpp | 默认运行；主机线程／FreeRTOS 桩，不测目标 SDK 时序 |
| 真实 TCP 监听、分段读写、资源与 API、连接释放和重开 | SocketHttpTransportTest.cpp | 可选；真实回环 socket、AppLoop 和生成网页，不是 Wi-Fi 实物测试 |
| 非阻塞采集调度、超时与恢复 | TemperatureServiceTest.cpp | 模拟采集，不测真实驱动耗时 |
| 连接超时、重试节奏、断网继续控制 | NetworkServiceTest.cpp | 模拟无线端口 |
| 红外精确映射、重复帧、过期及乱序 | InfraredInputTest.cpp | 合成编码，不测真实遥控协议 |
| 最终输出、命令排序、配置失败、红外接管与故障同轮处理 | AppLoopTest.cpp | 使用真实模块、模拟输出端口，响应在输出提交后发送 |
| M4 手机／按键／红外、断网重连和新启动联合场景 | AppLoopTest.cpp | 模拟端口联合演练，不代替手机与实物验收 |
| 连续 API 请求下的按键、红外和采集推进 | AppLoopPressureTest.cpp | 20,000 轮模拟时间，不测真实吞吐或连续运行 |
| 诊断格式、配置回退原因、故障、IP 与输出端繁忙 | TextDiagnosticsTest.cpp | 模拟日志接收端，不测真实串口缓冲或发送耗时 |
| UART 日志队列、发送停滞、容量与短写、停机清理 | Esp32SerialSinkTest.cpp | 主机线程／FreeRTOS／UART 桩，不测真实波特率、SDK 调度或电脑接收 |
| C++ 状态 JSON 与网页校验契约 | StateApiTest.mjs | 跨语言字段和响应格式 |
| 页面编入 C++ 后的字节一致性 | WebAssetsTest.mjs | 资源生成、编译和路由 |
| 网页串行写、旧响应、重启与未知结果 | WebClientTest.mjs | 模拟 fetch，不测浏览器布局 |
| 配置草稿、完整提交与保存确认 | ConfigDraftTest.mjs | 页面配置状态逻辑 |
| 模拟命令扁平 JSON 与重复字段拒绝 | CommandJsonTest.mjs | 默认入口运行，不需要监听端口 |
| 模拟 HTTP 配置原子性、同源限制、503、温度过期与慢正文准入 | PreviewApiTest.mjs | 可选；Node 模拟服务，不是 C++ HTTP 驱动 |
| 首版工单归档门槛 | ReleaseReadinessTest.mjs | 测试 25 张必需工单的状态、勾选与完成记录检查；不能验证证据真实性 |
| 板卡识别串口快照对比、启动失败、超时／取消清理、输出上限及失败留档 | BoardInspectionTest.mjs | 合成端口列表、真实设备名只读列举及自建 Node 子进程；不打开串口、不调用真实 esptool |

AppLoop 的新增组合回归验证：自动开启后红外切换进入手动；随后网页重新启用自动时，旧红外长按重复不接管；配置保存失败不改变输出、阈值或 revision，保存成功先重新计算输出再响应，重复保存不再写入；跨源写入无状态副作用；采集失效与手动开启同轮到达时先退出自动再执行手动，采集恢复不撤销后来的手动命令。

## 尚需实物执行的步骤

执行前先完成 ESP-001～004、007 的接线与固件准备。每条记录固件提交、BoardProfile、供电、时间和串口日志；未实施只记“未执行”。

1. 上电、复位及烧录：观察电机与 LED，核对启动关闭和上电瞬态，按 ESP-007 记录。
2. 按键和遥控：交替操作，遥控长按 3 秒并快速二次短按，确认实际编码、重复周期与输出；按 ESP-009／010 记录。
3. 手机网页：同一局域网打开设备地址，交替网页、按键和红外操作，断网再恢复，核对状态和拒绝提示；按 ESP-016 记录。
4. 真实温度与存储：观察阈值两侧及回差区域，模拟传感器失联后恢复，确认无自动重启；保存阈值、重启核对恢复，按 ESP-021 记录。Flash 断电恢复只在板端存储方案明确后执行。
5. 持续输入和连续运行：按 ESP-023 的至少 2 小时与 20 次开关基线，记录采样健康、本地响应、供电现象与重启原因。

以上不能从主机测试的通过结果推定完成，ESP-022 仍待前序实物验收和最终固件回归。

## 本轮结果

2026-11-01，识别工具增加 `--snapshot` 和 `--compare`，仅用 `/dev` 中的串口名称报告新增／消失端口，不自动选择设备或执行 ROM 读取。11 项识别工具测试及默认入口全部 37 项 Node 测试通过，覆盖多端口变化、无效快照拒绝和命令行完整流程；实际插拔板卡尚未执行。

2026-11-01，扩展 `build-arduino.mjs` 支持 WiFiBringup 和 ConfigBringup 两个诊断程序；使用 Arduino CLI 1.5.1／Arduino-ESP32 3.3.3 通用 ESP32 参考目标分别编译／链接通过，输入清单记录指定源码与占位凭据摘要。未知 sketch 名称拒绝；脚本语法与差异检查通过。本轮未改 C++／网页业务逻辑，未重跑主机套件；未连接设备、烧录或验证真实 Wi-Fi／NVS。

2026-11-01，完善 ROM 识别工具的失败报告与进程清理，新增 9 项 BoardInspectionTest 并纳入默认回归入口。运行默认入口所列的七个 Node 测试文件，35 项全部通过：覆盖正常／非零退出、缺失执行文件、双流输出截断、超时后退出 0、启动器与子进程忽略 SIGTERM、取消前／后的清理、固定命令参数及缺失端口报告。本轮只改主机工具、测试和说明，未重跑独立 C++ 套件、Arduino 参考构建或可选 HTTP／TCP；未连接、读取或烧录设备。

2026-11-01，新增 Esp32BootId 并替换 CompileCheck 的固定启动标识后，`sh firmware/test/run-host-tests.sh` 退出 0：23 个 C++ 测试程序（标识测试在三个独立进程运行）及 26 项 Node 测试通过。首次新增测试漏列真实 StateApi 所需的 ConfigStore 链接依赖，补齐后整套通过。验证启动前无随机调用、启用／采样／关闭顺序、128 位输入逐字节编码、前导零／全零、同次启动 1000 次缓存读取不碰硬件，以及真实状态 API 的 200 响应和原样输出。真实 Arduino-ESP32 参考构建退出 0，程序 1,200,523 字节、静态全局数据 50,140 字节。未重跑未修改的可选模拟 HTTP／TCP；未烧录或验证真实熵质量、实际重启和手机恢复。

2026-11-01，新增 Esp32SerialSink 后，`sh firmware/test/run-host-tests.sh` 退出 0：22 个 C++ 测试程序和 26 项 Node 测试通过。随后将格式拒绝检查移到空队列并补齐真实启动日志的容量检查，单独严格告警重编译运行 Esp32SerialSinkTest 通过。新增覆盖两次队列申请／任务申请失败与恢复、重复启动、4 条排队加 1 条发送中、停滞期间 10,000 次拒绝与丢弃计数、512 字节边界、所有权复制、FIFO／换行、短写停止后续发送、资源回收和真实 TextDiagnostics 串接。`node firmware/tools/build-arduino.mjs` 退出 0，22 个生产 C++ 文件已装配，真实参考目标生成 `build-zpsz3s/output/CompileCheck.ino.bin`，程序 1,199,191 字节、静态全局数据 50,092 字节；ELF 确认串口任务及提交方法已链接。本轮未改 HTTP／网页，未重跑可选模拟 HTTP／TCP；未烧录或进行实际串口时序验证。

2026-11-01，核对 6 张主板近照后新增 ROM 识别工具、增强 SerialBringup 并扩展构建入口。串口枚举和不打开设备的 dry-run 成功；烟测验证无参数、写／擦子命令、额外复位参数、非串口路径和未知 sketch 均拒绝。本机 esptool 5.1.0 的命令帮助接受固定只读参数。SerialBringup 和默认 CompileCheck 两个入口均在真实 Arduino-ESP32 3.3.3 参考目标编译／链接通过，分别占用 279,031 / 1,198,123 字节程序空间，静态全局数据 20,792 / 50,068 字节。默认集成产物目录 `firmware/build/arduino-check/build-JDTMK6/`；串口产物见[识别说明](serial-bringup.md)。本轮未修改业务控制代码，未重跑主机业务／TCP／模拟 HTTP 套件；未执行实际 ROM 读取、烧录或设备运行验证。

2026-11-01，新增隔离 Arduino 工具链及编译入口后，`sh firmware/test/run-host-tests.sh` 退出 0：21 个 C++ 测试程序和 26 项 Node 测试通过。`node firmware/tools/setup-arduino.mjs` 与 `node firmware/tools/build-arduino.mjs` 均退出 0，使用 Arduino CLI 1.5.1、Arduino-ESP32 3.3.3 在 macOS x86_64 完成通用 `esp32:esp32:esp32` 目标的真实编译／链接，全部 21 个生产 C++ 文件生成目标文件。产物、占用与摘要见[参考目标编译记录](arduino-reference-build.md)。未修改生产控制逻辑，本轮未重跑可选模拟 HTTP／回环 TCP；参考目标不代表实际选板，未烧录或进行设备验证。

2026-11-01，新增 NetworkHttpApi 并接入 AppLoop 后，`sh firmware/test/run-host-tests.sh` 退出 0：21 个 C++ 测试程序、26 项 Node 测试全部通过。生命周期测试覆盖地址变化、各请求阶段清理、旧响应丢弃、首次立即监听、5 秒重试与跨断网限频、时钟回绕、构造／销毁、每轮一次网络观察，以及等待恢复期间的按键和温度采集。`sh firmware/test/run-socket-tests.sh` 在已授权回环监听环境退出 0，真实 C++ TCP 路径已切换为 NetworkHttpApi，由模拟无线断开／同 IP 重连自动停止和恢复监听，网页／API 仍通过。未运行未修改的 Node 模拟 HTTP；未进行 ESP32 编译、烧录或实物验证。

2026-11-01，新增 SocketHttpTransport 和 Esp32SocketCloser 后执行 `sh firmware/test/run-host-tests.sh`，退出 0：20 个 C++ 测试程序、26 项 Node 测试全部通过，并编译 BSD 驱动。另运行 `sh firmware/test/run-socket-tests.sh`：首次因沙箱禁止监听而报 EPERM，授权回环监听后退出 0。新增真实 TCP 测试验证全部资源字节、HEAD、写入与状态读取、跨源拒绝、截断半关闭、空闲客户端期间按键推进、端口冲突、256 字节驱动预算、延迟关闭和监听重开；关闭任务测试验证固定资源、停滞／错误与清理。本轮未改 Node 模拟器，未运行其可选 HTTP 测试；没有目标工具链编译或实物验证。

2026-11-01，新增 HttpApiPort 后执行 `sh firmware/test/run-host-tests.sh`，退出 0：19 个 C++ 测试程序、26 项 Node 测试全部通过。新增连接测试以关闭异常／RTTI 的 C++11 严格告警构建，使用真实生成资源及 AppLoop，覆盖全部资源短写后的字节一致性、HEAD／路由／响应头、接收与响应期限和时钟回绕、收发失败、响应所有权与上限、网络关闭清理、慢读者期间按键接管，以及流水线命令不执行。首次测试漏掉按键启动释放消抖阶段，补齐测试时序后整套通过；控制规则未改。默认跳过 Node 模拟 HTTP；本轮没有真实 TCP、ESP32 工具链或设备验证。

2026-10-30，新增 HttpRequestParser 后执行 `sh firmware/test/run-host-tests.sh`，退出 0：18 个 C++ 测试程序、26 项 Node 测试全部通过。新增验证全部双片切分位置、逐字节输入、逐位置截断、重复／歧义头、长度溢出、精确容量边界、固定接收预算、总期限与时钟回绕、流水线尾部不交付，以及原始开关／配置请求经真实 WriteApi 的处理。默认入口跳过模拟 HTTP 测试；本轮未改 Node 模拟器或网页，未进行实际 TCP 或设备验证。

2026-10-30，完成 NVS 适配和保存结果未知处理后，`sh firmware/test/run-host-tests.sh` 退出 0：17 个 C++ 测试程序、26 项 Node 测试全部通过。`node --test firmware/test/PreviewApiTest.mjs` 在允许本地监听的环境退出 0，5 项 HTTP 测试通过。独立后台 Chrome 通过未知结果提示、保存禁用、草稿保留、手动控制及刷新后保持暂停检查，并目视核对手机宽度截图。最终补齐主循环保存暂停日志后，单独重新编译运行 AppLoopTest 与 TextDiagnosticsTest，均通过，验证首次记录及重复提交不刷屏。仍未执行目标工具链编译、烧录或真实 Flash 恢复。

2026-10-30，ESP-018 补齐配置记录编解码，运行 `sh firmware/test/run-host-tests.sh` 退出 0：16 个 C++ 测试程序和 23 项 Node 测试通过。新增覆盖独立字节样本、精度保留、非法写入、截断／尾随字节、224 个单比特翻转，以及字节后端的版本／阈值校验、保存失败与新实例恢复。可选模拟 HTTP 测试按默认入口跳过；未执行板端编译、Flash 写入或真实断电恢复。

2026-10-30，ESP-004 增加 Arduino GPIO 端口的主机桩验证；修正预置电平实现后运行 `sh firmware/test/run-host-tests.sh` 退出 0：14 个 C++ 测试程序、23 项 Node 测试通过，模拟 HTTP 测试按默认入口跳过。新增测试覆盖关闭电平先于输出模式、输入上拉及电平读写。串口最小程序尚未使用实际 Arduino-ESP32 工具链编译或烧录。


2026-10-30，新增 BoardProfile 和 GPIO 端口适配的主机测试，运行 `sh firmware/test/run-host-tests.sh` 退出 0：13 个 C++ 测试程序、23 项 Node 测试通过；可选模拟 HTTP 未运行。新增测试覆盖缺失／冲突／不可用引脚拒绝、M4／M5 温度能力、无效配置不触碰 GPIO、风扇先关闭的初始化顺序、按键和输出极性。此结果不代表 Arduino 工具链、GPIO 电平或实物电机验证。


2026-10-29，ESP-020 浏览器温控联调覆盖：扩展 `node firmware/test/WebUiSmoke.mjs` 并在专用 Chrome／新模拟器执行通过。既有表单检查及新增自动回差、无效温度、自然过期、恢复保持手动、故障期间手动接管均通过。本轮未修改业务代码，未重跑主机套件，未进行真实设备验证。

2026-10-29，ESP-020 浏览器表单验收补齐：新增可选 `node firmware/test/WebUiSmoke.mjs`，在独立无界面 Chrome 与新启动的专用模拟器上执行通过。覆盖手机宽度、阈值校验、轮询草稿保护、保存确认、断线恢复和不自动保存；另完成截图目视检查。运行方法及端口、初始状态要求见 [浏览器验收说明](web-ui-smoke.md)。本轮只增加测试与文档，未重跑无变化的主机套件，未进行硬件验收。

2026-10-29，ESP-015 请求恢复覆盖补齐：`node --test firmware/test/WebClientTest.mjs` 的 17 项测试全部通过。新增正文停滞、失效轮询晚到失败、写入与恢复查询连续失败三类异步场景；验证超时隔离、历史状态保留、请求锁释放和不重发。本轮没有业务代码变化，仅运行受影响测试套件，未执行实物验收。

2026-10-29，ESP-015 同版本冲突隔离：新增测试先复现相同 bootId/revision 却改变输出的响应被接受；修复后 `sh firmware/test/run-host-tests.sh` 退出 0，11 个 C++ 测试程序和 20 项 Node 测试全部通过。新增覆盖输出、阈值、来源、联网及健康字段冲突、有效状态恢复、样本年龄重置及写响应冲突不重发。可选 HTTP 测试未运行，未执行设备验证。

2026-10-29，ESP-020 模拟器重复字段校验：`node --test firmware/test/CommandJsonTest.mjs firmware/test/PreviewApiTest.mjs` 退出 0，1 项解析测试和 4 项 HTTP 测试全部通过，覆盖重复字段、转义后重复名称、嵌套值拒绝、字符串值边界与非法命令无状态副作用。新解析测试纳入默认回归入口；脚本语法、解析模块语法和差异空白检查通过。本轮未修改 C++ 或网页代码，未重跑相应套件，未执行实物验收。

2026-10-29，ESP-020 模拟器慢正文修复：分段上传自动模式命令、延迟 5150 ms 完成正文，先复现样本过期仍返回 200；修复后返回 409。`node --test firmware/test/PreviewApiTest.mjs` 的 4 项 HTTP 测试全部通过，模拟器语法检查与 `git diff --check` 通过。本轮只改模拟器及测试，未重跑未改动的 C++／网页套件，未执行板端验证。

2026-10-29，ESP-012 状态契约补齐：新增跨语言场景先复现自动模式无健康样本仍返回 200 的问题；修复后 `sh firmware/test/run-host-tests.sh` 退出 0，11 个 C++ 测试程序和 18 项 Node 测试全部通过。覆盖五种模式／健康／故障矛盾组合，以及温控故障后手动开启的合法状态。本轮未改模拟 HTTP 服务，未重跑可选 HTTP 测试，未执行实物验收。

2026-10-29，ESP-015 重启响应隔离修复：新增用例先复现“写响应已观察到新启动、后续旧启动查询仍被接受”的问题，修复后 11 个 C++ 测试程序和 18 项 Node 测试全部通过。完整入口中的模拟 HTTP 测试因沙箱禁止本地监听（EPERM）失败；随后在允许监听的环境单独运行 `node --test firmware/test/PreviewApiTest.mjs`，3 项全部通过。未执行设备重启或串口验证。

2026-10-29，补齐 ESP-008 文本诊断后再次运行完整入口 `sh firmware/test/run-host-tests.sh --with-preview`，退出 0：11 个 C++ 测试程序、16 项 Node 测试和 3 项模拟 HTTP 测试全部通过。新增诊断测试验证配置回退原因、控制输出、故障、IP、无事件静默、标识符长度和换行处理，以及输出端拒绝后的丢弃计数与恢复；未验证实际串口。

2026-10-29，继续开发前运行 `sh firmware/test/run-host-tests.sh --with-preview`，退出 0：10 个 C++ 测试程序、16 项 Node 测试和 3 项模拟 HTTP 测试全部通过，包括温度注入、过期与故障恢复场景。本轮未新增功能；板端实现仍等待 ESP-001～003 的型号、原理图、GPIO 与驱动供电确认，未执行烧录或实物验收。

2026-10-28，ESP-022 默认入口与 `--with-preview` 完整入口均退出 0：当时 9 个 C++ 测试程序、14 项 Node 测试和完整入口额外的 2 项模拟 HTTP 测试全部通过。ESP-023 后续将主机压力测试加入默认入口。`sh -n` 语法检查通过，未知参数以退出码 2 拒绝。未执行任何板端或实物验收。
