# 非阻塞温度采集调度

ESP-017 的 TemperatureService 已实现硬件无关的转换调度、超时、重试与诊断。TemperaturePort 是真实温度驱动的接入接口；当前只有测试用模拟实现，尚未选定传感器库、GPIO 或完成实物采集。

## 接口分工

`TemperaturePort::start()` 发起一次新转换并丢弃旧结果；失败返回 false，且不得遗留活动操作。`poll(value)` 返回 Pending、Ready 或 Invalid：Pending 不提供有效温度，Ready 提供本次转换的新结果，Ready／Invalid 均结束当前操作。`cancel()` 终止当前操作并清除所有迟到／缓存结果，防止下一轮把旧样本当成新样本。

三个方法都必须快速返回，不得睡眠或等待转换完成。器件通信、校验和、断线值、量程与上电占位值检查由真实驱动负责；服务另行拒绝 NaN 和无穷。不能直接把通用读取函数包装成 poll，却让该函数内部阻塞整个主循环。

TemperatureService 不访问 GPIO、不决定电机输出，仅返回 None、Sample 或 Invalid 事件。转换失败、读取失败、非有限值、超时、非法时序配置分别提供诊断枚举，供 App 记录串口日志。

## 调度规则

默认每 1000 ms 发起一次转换，单次转换超时暂设 1000 ms。这是待器件资料和实测确认的工程默认值，不是对实际 18B20 型号或转换时长的认定。两项可经 TemperatureTiming 设置，必须大于 0 且小于 2³¹ ms；非法配置仅报告一次诊断，并停止调用驱动。

首次 update 只发起转换，不在同次调用等待或读取。转换期间每次 update 至多轮询一次；达到超时边界先取消，不接受已经迟到的 Ready。结果或错误返回后，本次 update 不立即重试，下一轮依据距上次启动的时间决定是否重新发起。启动失败也按间隔限频，不逐循环重试。

经过较长调度延迟后只发起一次新转换，不补发积压周期。计时采用 32 位无符号单调毫秒差，支持自然回绕；主循环不能停顿整个时钟周期。服务不拥有后台线程，也不保证驱动方法本身非阻塞，这一项需要板端验证。

## App 装配顺序

[AppLoop](app-loop.md) 已实现以下软件装配顺序：每轮先处理采集事件，再推进控制器健康时钟，再处理用户命令和最后一次硬件输出：

```cpp
const auto event = temperatureService.update(nowMs);
if (event.kind == TemperatureEventKind::Sample) {
    controller.recordTemperature(event.temperatureC, nowMs);
} else if (event.kind == TemperatureEventKind::Invalid) {
    controller.invalidateTemperature(nowMs);
}
controller.tick(nowMs);
// 随后处理按键／红外／HTTP 命令，最后提交 controller 的最终输出。
```

即使该轮没有新样本，也必须推进控制器时钟。没有启用采集服务时也要保留 tick 调用；它使旧样本在超过 5 秒后过期。主循环整体停住时，任何软件时钟检查都无法继续运行，不能据此宣称设备具备停机保护。

仅 Sample 事件更新时间；不能反复提交缓存值，否则会伪装样本一直新鲜。样本时间记录为本轮取得有效结果的时刻。当前无有效温度返回 null；曾有有效样本时仍保留其年龄。驱动明确失败或超时为 invalid；没有新的采集事件而健康时钟持续推进时，旧健康样本超过 5 秒为 stale。自动模式的关闭、故障和恢复规则由 FanController 统一处理。

## 验证与剩余工作

运行 `sh firmware/test/run-host-tests.sh`。新增 TemperatureServiceTest.cpp 使用可控时钟与模拟驱动，覆盖首次初始化、等待期间按键／网络操作、单次样本消费、采样间隔边界、错误关闭与恢复不自动开启、超时取消、迟到结果丢弃、非有限值、失败限频、时钟回绕、无补发风暴、非法时序及采集停滞的 stale 行为。

仍需实物确认温度型号、引脚与上拉要求，锁定库版本，选择非阻塞转换调用，验证通信错误与器件特殊读数，并测量主循环响应与采集时序。主机测试不证明真实驱动非阻塞，不替代温度变化或断线实物验收。ESP-017 保持进行中。
