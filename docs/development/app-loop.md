# 主循环装配与最终输出

ESP-008 的硬件无关 AppLoop 已将控制器、按键消抖、配置加载、温度调度、网络调度和 C++ API 处理接成一条主循环。OutputPort、ApiPort、DiagnosticPort 仍是板端接入接口；测试使用模拟端口，尚不能直接运行在 ESP32 或宣称实物风扇可用。

## 启动顺序

创建对象时只保存依赖，不发起网络或采集。板端也应避免在依赖构造函数中进行耗时初始化。调用 start 后依次：

1. OutputPort.initializeOff 建立关闭输出。
2. 将状态 LED 设为关闭。
3. 加载 ConfigStore，使用校验后的阈值构造本次启动控制器。
4. 记录加载结果与启动状态。

start 重复调用无效果，不重复加载或重置运行状态。未 start 时 step 返回 false，不调用任何端口。bootId 仍由板端启动代码提供非空且每次启动不同的标识；本次不实现真实随机源。

OutputPort.initializeOff 的正确电平和 GPIO 初始化顺序属于 ESP-007。主机测试只能证明调用顺序，无法保证芯片复位、烧录或软件尚未启动阶段的电机关闭，需要驱动硬件设计与实测。

## 每轮顺序和预算

调用 `step(nowMs, buttonPressed)`，buttonPressed 已由板端依据有效电平归一化。处理顺序为：

1. 有采集服务时，推进一次转换并提交新样本／无效事件。
2. 无论是否启用采集，都调用控制器 tick 更新温度健康。
3. 有网络服务时推进一次连接状态，更新快照及受信任设备地址。
4. 扫描按键消抖，产生一次按下事件时执行控制器切换。
5. 有红外输入时读取最多一帧，经映射与重复帧过滤后提交控制器。
6. 用同一轮网络结果调用 ApiPort.update，推进 HTTP 生命周期及有限 I/O，再取最多一个完整 API 请求，执行 C++ 状态读取或写处理。
7. 按最终快照提交风扇和 LED，随后记录变化，再返回 API 响应。

同轮同时观察到按键和 API 时，先处理按键，再处理红外，最后处理 API；这是明确的接收顺序，不推断物理事件在外部发生的精确时间。不会逐个中间状态写 GPIO。例如按键切为开，而同轮 API 显式关，则最终只提交关，不产生短暂开脉冲。温度失效先于自动模式准入，因此同轮过期样本不能用于开启自动。

每轮温度与网络各一步、按键一次、红外最多一帧、API 最多一个，没有按请求队列长度循环处理。ApiPort.takeRequest 必须非阻塞，接收缓冲有界且限制每次读取量；sendResponse 必须有界排队，不能等待慢客户端读取。队列满时的拒绝／断开策略及实际毫秒预算仍由板端 HTTP 适配器实现，不能仅凭“一次调用”宣称所有 I/O 都不阻塞。

ApiPort 只交付 API 路由。静态页面由 HTTP 适配器使用已生成的 WebAssets 单独提供，也要受处理预算限制。[NetworkHttpApi 与 socket 驱动](http-connection.md) 已提供该装配：将 NetworkHttpApi 作为 ApiPort 传入后，只调用 app.step；不额外更新 NetworkService 或手动 poll。每轮网络结果同时用于可信来源更新与 HTTP 启停，断网／换地址时先丢弃旧连接，再尝试取请求。监听尝试间隔至少 5000 ms；稳定监听时单客户端、读写每轮最多 256 字节，响应总期限 5000 ms。

ApiPort.update 默认空实现，原有模拟 API 端口不受影响。SocketHttpTransport 使用非阻塞 BSD socket，ESP32 的 close 交给固定任务。真实回环 TCP 已验证自动断网关闭和同 IP 重连后的 HTTP 恢复；SDK 调度与设备耗时仍需实测。AppLoop 本身没有替代 Arduino 的 setup/loop 启动入口。

## 输出与日志

风扇与 LED 读取同一最终快照。只在输出值变化时调用 setFan 和 setIndicator，避免空闲循环重复写；这些接口表示已提交的控制信号，不代表测到风扇转动。

DiagnosticPort 接收启动加载结果、温度错误、网络事件，以及模式／输出／故障／命令来源／阈值变化。相同状态、仅采样年龄或温度数值变化不会逐循环打印状态日志。

`adapters/TextDiagnostics.h/.cpp` 实现可复用的文本诊断格式。构造 `TextDiagnostics(sink)` 后作为 AppLoop 的 DiagnosticPort 传入；每次传入一整行（不带换行符）。格式层不重试，输出端拒绝时累计到 `droppedLines()`（达到 uint32 最大值后饱和）。板端输出由 `board/Esp32SerialSink.h/.cpp` 提供，真实串口时序仍待实物验证。

### ESP32 UART 诊断发送

`Esp32SerialSink` 接收已初始化的 `HardwareSerial&`，对应当前 USB-C／CH340C 核心板的 UART 路径。在 `Serial.begin(115200)` 后、`AppLoop.start()` 前调用 `sink.start()`；初始化失败时所有日志提交返回 false，本地控制可以继续。成功后将该 UART 的写入所有权交给 sink，其他任务不能混用 `Serial.print`、调试输出或在运行中调用 `Serial.end()`；底层 SDK 如另有日志输出，也需在正式固件装配中核对其 UART 路由。

- 一个控制循环生产者；最多排队 4 条，加工作任务正在发送的 1 条。每条正文最多 512 字节，另加一个换行；空行允许，CR／LF／NUL 和超长正文被拒绝，防止破坏整行格式。
- 提交只复制固定大小记录并用 `xQueueSend(..., 0)` 入队，不访问 UART、不分配动态队列、不等待发送完成。队列满或尚未启动时立即返回 false；持续拥塞不会增长缓存。
- 一个优先级 1、栈 3072 字节的 FreeRTOS 工作任务按 FIFO 调用 UART 写入。Arduino-ESP32 3.3.3 的 UART 写路径存在互斥锁和 `uart_write_bytes`，因此不在控制线程通过 `availableForWrite()` 加 `write()` 来假定无等待。
- 如果串口接口报告短写，`writeFailed()` 锁定为 true，后续提交拒绝，已排队但未发送的记录丢弃，不重试或接着拼接残缺行。Arduino 3.3.3 的 `HardwareSerial::write` 本身返回请求长度，**不能据这个返回值证明数据已到达电脑**；短写检测仅处理接口明确报告的失败。
- `tryWriteLine=true` 表示已接收入队，不是电脑确认。格式层的丢弃计数只统计被拒绝的提交，不覆盖入队后传输失败。此诊断通道不作为持久化审计日志。
- 析构会等待已接受记录处理、工作任务确认退出，再释放两条队列；可能等待 UART，只能在控制循环外停机时执行。UART 对象必须比 sink 活得更久；没有强制终止正在写 UART 的任务。

当前 CompileCheck 已将原先的静默输出替换为此发送端。它仍为无电机 GPIO、占位 Wi-Fi 的编译检查程序，不据此声称整机固件完成。主机线程／FreeRTOS／UART 桩测试覆盖资源申请失败、重试启动、拥塞、FIFO、边界长度、正文复制、格式拒绝、短写锁定与清理；实际波特率、任务栈余量、SDK 日志干扰和断开终端后的行为待设备验证。

日志示例：

```text
startup config=missing_defaults boot=boot-1 revision=0 mode=manual output=off source=system fault=none on_c=30 off_c=28
network event=connected connected=true ip=192.168.1.2
temperature event=timeout
```

启动日志区分已加载、缺失、损坏、格式版本不匹配和读取失败；状态日志记录模式、控制输出、命令来源、故障和阈值。网络断开时 IP 输出 `-`，不继续展示旧地址。无诊断事件时不输出；bootId 和地址限制为 128 字符并替换空白及控制字符，避免一条事件变成多行。此接口不接收 Wi-Fi 密码。它记录 AppLoop 产生的最终状态变化，不是逐条命令审计日志。

网络动态地址每轮从可信静态策略重新构建，IP 改变或断开时不会保留旧动态 origin。可信静态主机名可以通过构造参数配置，不会由收到的 Host 建立。设备实际 IP 仍由已核实的无线驱动提供。

## 可选能力与生命周期

温度、网络、红外和 API 服务指针可为空，保留独立的按键手动控制。控制器由 AppLoop 持有，外部只读取快照；所有命令通过该循环串行处理。各端口、ConfigStore 和可选服务必须比 AppLoop 活得更久，不能在后台线程并发调用。红外映射与过滤已接入，见 [红外输入](infrared-input.md)；真实解码驱动与遥控映射仍待 ESP-009 实测。

## 验证

`sh firmware/test/run-host-tests.sh` 包含 AppLoopTest.cpp：模拟输出端检查关闭先于存储加载；模拟 API 确认响应发生在最终输出提交之后；真实 C++ 控制器处理同轮按键与网页、温度故障后的自动拒绝、手动接管、IP 更新后的旧地址拒绝，以及持续请求下的按键／采集推进。另验证没有网络和传感器时的纯本地装配，以及红外切换、重复帧不覆盖按键来源、同轮网页最终命令不产生中间输出。

GPIO、LED 电平、驱动供电、串口发送时序与真实独立运行尚未完成实物验证，因此 ESP-008 保持进行中。有界串口和网络适配已接入参考目标编译；下一步仍需实际 BoardProfile、ESP-007 供电／输出确认及设备验证。
