# 手动控制器开发与验证（ESP-006）

## 当前范围

`firmware/src/domain/FanController.h` 和 `.cpp` 实现纯 C++11 手动控制规则，不依赖 Arduino、GPIO、HTTP 或红外库。依据 2026-10-28 用户要求，等待硬件资料期间先实现可在电脑执行的部分。ESP-004 的板型、固件工具链和烧录依赖仍未完成，本模块尚未接入固件。

执行测试：

```sh
sh firmware/test/run-host-tests.sh
```

脚本使用本机 `c++`（可通过 `CXX` 指定编译器），启用 `-std=c++11 -Wall -Wextra -Wpedantic -Werror`，在临时目录编译并运行，结束后清理产物；统一入口也运行网页测试，需要 Node.js。无需 CMake 或第三方库。2026-10-28 在 Apple clang 15.0.0 上通过；这不表示已通过 ESP32 编译或实物验收。

## 命令与状态约定

- `setPower(on, source)`：显式开／关并进入手动模式。
- `setMode(Manual, source)`：保留当前输出；`Auto` 仅在已有有效且未过期温度时接受，否则返回 `TemperatureUnavailable`。
- `toggleFromButton()`：在控制器内部读取当前输出并切换，来源为 `Button`。输入消抖属于 ESP-005。
- `setWifiConnected(connected)`：更新网络状态，保留模式、输出与命令来源。
- `snapshot()`：返回状态副本，调用者不能借此修改控制器内部状态。
- 未知枚举值返回 `InvalidCommand`；拒绝的命令不改变任何状态字段。HTTP 参数解析和状态码映射属于后续适配器。

由 App 在单一控制循环中按接收顺序调用，最后接受的命令决定状态。控制器不含并发队列或锁，不得从多个线程／异步回调直接调用；异步入口需要先投递到主循环。

`fanOutputOn` 在尚未接入 FanOutput 的主机测试中仅表示控制器决定的输出值，不能解释为硬件已收到电平或电机已转动。

## 启动标识与版本

每次启动由 App 构造一个新控制器，并传入非空的新 `bootId`。标识在该控制器生命周期内不变；不得将文档或测试中的固定值用于固件。板端由 `board/Esp32BootId.h/.cpp` 的 `esp32BootId()` 生成，已接入 CompileCheck；控制器本身仍只接收字符串，不依赖随机硬件。

### ESP32 启动标识生成

首次调用采集 16 字节随机数据，按字节顺序编码为 32 个小写十六进制 ASCII 字符，保留前导零。采用函数内静态缓存，同次启动后续调用不再读取随机源，不随 Wi-Fi 重连、状态变化或 AppLoop.start 重复调用而变化。标识不含 MAC 地址，不使用时间戳或固定 fallback，不读写 NVS。

首次调用必须位于启动代码中，早于 ADC、I2S、Wi-Fi／Bluetooth 使用，也不能由全局构造函数或并发任务首次触发。参考入口在 `setup()` 首先取得标识，然后创建 AppLoop；网络连接仍在后续循环推进。若正式板型或库在 setup 之前已经启用相关外设，必须重新核对初始化顺序，不能照搬此入口。

根据 [ESP-IDF 5.5 随机数文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/system/random.html)，未启用 RF／内部熵源时直接读取 RNG 只能视为伪随机。实现依次调用 `bootloader_random_enable()`、`esp_fill_random(..., 16)`、`bootloader_random_disable()`，释放内部 ADC 熵源后才分配和格式化字符串；这段有界硬件采样只在启动时执行，不放入控制循环。

这是用于区分启动会话的 128 位随机标识，不是认证凭据，也不是数学上绝不重复的持久化计数器。SDK 采样接口无失败返回值，本模块未增加硬件随机源健康检测；主机测试只能验证调用顺序、编码、缓存和契约，不能证明真实熵质量或设备重启结果。完整实物验收仍需记录多次断电／复位日志并确认网页丢弃旧会话响应。

`revision` 使用无符号 64 位整数，初始为 0。每次被接受的命令若改变公开状态（包括 `lastCommandSource`），版本加 1；同来源的相同开关命令不加版本。同一个输出值来自不同来源时，来源改变，因此版本加 1。网络连接状态变化也加 1；查询、重复网络通知和被拒绝命令不加版本。

启动默认阈值为 30／28℃，`hasTemperature=false`、`hasSample=false`。HTTP 适配器必须把不可用的 `temperatureC`、`sampleAgeMs` 输出为 JSON `null`，不能把内部占位值 0 当成有效测量。ESP-019 已实现注入采样和时钟推进，年龄变化不增加业务版本，温度、健康状态或故障变化才推进版本。真实采集仍待 ESP-017；调用顺序见 [自动温控与故障恢复](automatic-control.md)。

## 验证边界与后续工作

ESP-018 已接入配置：启动时将 `ConfigStore::load()` 后的 `current()` 传给控制器构造函数；运行时调用 `setConfig(store, config)`，持久化成功后才更新两个阈值与 revision，失败保持旧状态。具体契约和未完成的板端存储见 [温控配置与存储边界](config-store.md)。构造参数若非法则保留默认阈值，正式 App 应通过 ConfigStore 获取已校验配置并记录加载诊断。

主机测试覆盖启动默认值、重复开关、多来源排序、按键切换、无温度自动模式拒绝、手动模式保留输出、版本推进、断网保持输出、非法枚举和状态副本隔离。使用两个注入的测试标识验证新会话初始化，不代表板端随机源或真实重启已验证。

板端 bootId 生成和参考启动装配已实现；正式 FanOutput／接线装配、真实重启及网页联调仍待完成。ESP-006 保持进行中；ESP-002～005 的硬件依赖不因此解除。
