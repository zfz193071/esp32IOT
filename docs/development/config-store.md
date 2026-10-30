# 温控配置与存储边界

ESP-018 已实现配置校验、加载回退、保存流程、控制器接入、固定字节编解码及 ESP-IDF NVS 适配。NVS 调用和故障恢复目前通过主机桩验证；实际工具链编译、Flash 读写、重启与断电恢复尚待设备验证。

## 配置规则

`TemperatureConfig` 仅含开启阈值 `onThresholdC` 和关闭阈值 `offThresholdC`，默认值为 30／28℃。两者必须是有限数值，均在 0～60℃ 内，且开启阈值至少高于关闭阈值 1℃。支持小数；拒绝 NaN、无穷、越界、反向阈值和不足 1℃ 的间隔。

`ConfigRecord` 包含版本号 1 和完整的一对阈值，不包含运行模式、输出开关、Wi-Fi 凭据或启动标识。它是逻辑记录，不可直接将 C++ 结构体内存写入 Flash；底层适配器负责明确字节格式、长度与完整性校验。

## 持久化字节格式

`firmware/src/config/ConfigRecordCodec.*` 提供供后续存储适配器调用的编解码函数，不依赖 Arduino 或 Flash API。记录固定为 28 字节，数字按小端顺序写入，不保存 C++ 结构体填充字节。

| 字节偏移 | 长度 | 内容 |
|---|---:|---|
| 0 | 4 | ASCII `FAN1`，识别本记录封装 |
| 4 | 4 | 无符号 32 位配置版本，当前写入 1 |
| 8 | 8 | 开启阈值，IEEE-754 binary64 |
| 16 | 8 | 关闭阈值，IEEE-754 binary64 |
| 24 | 4 | 对前 24 字节计算的 CRC-32/ISO-HDLC |

CRC 使用反射多项式 `0xEDB88320`，初值和最终异或均为 `0xFFFFFFFF`；校验值同样以小端保存。阈值保留现有 `double` 的精度，未引入小数位舍入；编译时要求平台提供 IEEE-754 binary64 `double`。

`encodeConfigRecord()` 只接受版本 1 和有效阈值，失败不修改输出缓冲区。`decodeConfigRecord()` 要求非空地址、精确长度、正确标识及 CRC；失败返回 `Corrupt` 且不修改目标记录。通过完整性检查后返回 `Found`，由 `ConfigStore::load()` 继续检查配置版本和阈值，保留 `VersionDefaults` 与 `CorruptDefaults` 的区别。`Found` 本身不能作为阈值合法的依据。改变记录封装布局时必须另行定义格式与迁移规则，不能只修改版本号后继续按旧偏移解释。

存储适配器区分记录缺失 (`Missing`) 和读取失败 (`Unavailable`)，完整读取后再调用解码，写入前编码完整阈值对。CRC 用于检测数据损坏，不提供提交确认或回滚能力；NVS 调用的成功和错误需按下述结果分类处理。

## 启动加载

启动顺序：创建板端存储适配器 → 创建 `ConfigStore` → 调用 `load()` 并记录结果 → 用 `store.current()` 构造 `FanController`。控制器始终从手动、关闭、revision 0 开始，配置只影响阈值。

| 加载结果 | 行为 |
|---|---|
| `Loaded` | 使用版本与数值均有效的记录 |
| `MissingDefaults` | 记录不存在，使用默认值 |
| `CorruptDefaults` | 底层完整性检查失败或阈值非法，使用默认值 |
| `VersionDefaults` | 格式版本不支持，使用默认值 |
| `ReadFailureDefaults` | 存储不可读，使用默认值并禁止写入未知旧记录 |

本项目加载和回退代码不会自动写入或修复存储。`load()` 是启动阶段的幂等操作，重复调用返回首次结果，不重新读取或解除保存暂停；通过重启重建适配器、ConfigStore 与控制器再执行恢复。App 的文本诊断包含加载结果和 `config_save_blocked`，完整板端日志装配仍待完成。

## 保存与控制器更新

运行期间通过 `FanController::setConfig(store, config)` 提交完整阈值对，不直接调用 store 改写控制器背后的配置。一个 App 会话使用同一个 ConfigStore 与控制器，所有调用由主循环串行执行。

先校验配置，再请求后端替换完整记录，后端确认成功后才更新 ConfigStore 和控制器中的阈值；公开阈值变化时 revision 加 1。手动输出、模式与最后开关命令来源保持不变。ESP-019 已实现自动模式下新阈值的立即重算；自动输出变化时来源记为 system，与阈值修改合并推进一次版本。

| 保存结果 | 行为 |
|---|---|
| `Saved` | 一次完整记录写入成功，更新运行阈值 |
| `Unchanged` | 与当前阈值相同，不写入、不推进已有一致状态的版本 |
| `Invalid` | 两个值全部拒绝，不写入或部分更新 |
| `StorageFailure` | 不改变运行阈值，要求后端保留旧可恢复记录 |
| `StorageUnknown` | 存储可能是旧值或新值；保留当前运行阈值，本次运行暂停配置保存，重启后核实 |
| `NotLoaded` | 尚未启动加载，拒绝覆盖未知存储 |

损坏／缺失记录回退默认值后，显式提交相同默认值仍不触发写入；下次启动可能再次记录相同回退诊断。显式提交不同的有效阈值可以建立新记录。读取失败属于存储不可用，即使提交相同值也返回失败，避免报告存储正常。

2026-10-30 用户确认新增“保存结果未知”。`ConfigPersistence::replace()` 返回 `Saved`、`Failed` 或 `Unknown`：只有 `Failed` 承诺旧可恢复记录保持不变，`Unknown` 不承诺回滚。ConfigStore 遇到未知结果后，所有后续合法配置提交（包括与当前相同的值）均返回 `StorageUnknown`，不再次触碰存储。控制器首次将 `configSaveBlocked` 置为 true 并推进一次 revision，保留阈值、输出、模式和来源；重复提交不再推进版本。自动温控继续使用旧阈值，开关、模式、采集和网络正常工作。此标记不写入 Flash；重启后从实际存储恢复阈值，仍从手动关闭开始。

## NVS 适配与板端实验

`firmware/src/board/NvsConfigPersistence.*` 使用默认 `nvs` 分区的 `esp32fan` 命名空间、`temperature` blob 键。应用主循环独占该命名空间，平台在首次加载前完成 NVS 初始化；适配器本身不初始化、擦除、修复分区或重试错误。

- 读取以 `NVS_READONLY` 打开；命名空间／键不存在返回 `Missing`，不会创建命名空间。单次 `nvs_get_blob` 使用固定 28 字节缓冲区，长度／类型／CRC 错误返回 `Corrupt`，其余驱动错误返回 `Unavailable`。所有成功打开的句柄都关闭。
- 保存先编码，再以 `NVS_READWRITE` 打开并执行 `nvs_set_blob` 和 `nvs_commit`；只有两者均成功才返回 `Saved`。编码／打开失败尚未写目标记录，返回 `Failed`。任何 set／commit 报错保守返回 `Unknown`，不补写或尝试回滚；适配器自身也锁住后续读写，直到重启重建实例。
- 依据 [ESP-IDF 5.5 NVS 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/storage/nvs_flash.html)，`ESP_ERR_NVS_REMOVE_FAILED` 可能在新值已写入后返回，并在重新初始化时完成更新。[实现源码](https://github.com/espressif/esp-idf/blob/v5.5/components/nvs_flash/src/nvs_handle_simple.cpp)也表明 set 会进入存储写入，不能把关闭句柄当作事务回滚。主机故障注入覆盖旧／新记录两种结果，不等于实际断电证明。

`firmware/arduino/ConfigBringup/ConfigBringup.ino` 提供串口独立实验，波特率 115200，不配置电机 GPIO，不在启动时写阈值。实物板型、工具链及串口确认后，打开此程序编译上传：`p` 查看当前阈值，`s` 显式保存 35.5／32.25℃，`d` 显式保存 30／28℃。保存确认后手动重启，检查启动加载结果和阈值；未知结果时保存保持暂停，重启后以实际读取值为准。`d` 不擦除分区，相同值不重复写入。本程序尚未经过目标工具链编译或烧录。

Arduino 核心启动行为需随最终工具链核对：[Arduino-ESP32 3.3.3 的 `initArduino()`](https://github.com/espressif/arduino-esp32/blob/3.3.3/cores/esp32/esp32-hal-misc.c)会初始化默认 NVS，并在无空闲页或格式版本不支持时擦除重建分区。这是核心启动逻辑，发生于本适配器之前，因此不能把“适配器不擦除”理解成所有启动异常均保留旧记录。最终设备验收须记录核心版本和初始化日志。

## 验证记录

2026-10-28 运行 `sh firmware/test/run-host-tests.sh`：配置测试、原有控制器与消抖测试，以及 7 组网页测试全部通过。

配置测试覆盖：有效小数、0／60℃ 边界、精确 1℃ 间隔、NaN／无穷拒绝、重复配置无写入、完整阈值更新、写失败后新实例恢复旧值、缺失／损坏／未知版本回退、不可读存储禁止写入，以及新控制器只恢复阈值且保持手动关闭。

新实例测试不等于真实断电或烧录重启。ESP-018 保持进行中，等待 ESP-008 完整板端装配、目标编译和实物恢复验收；HTTP 配置接口和网页设置属于 ESP-020。

2026-10-30 新增 `ConfigRecordCodecTest.cpp`：使用 Python `struct`／`zlib` 独立生成的固定样本校验字节序与 CRC；覆盖边界和高精度小数、非法写入不改缓冲区、全部截断长度、尾随字节、空指针、记录 224 个单比特翻转、CRC 正确但版本未知／阈值非法，以及字节后端与 ConfigStore 的保存、去重、写失败和新实例恢复。测试纳入统一主机入口；内存后端只模拟原子替换契约。
