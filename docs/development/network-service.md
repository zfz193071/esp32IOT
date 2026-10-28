# Wi-Fi 连接与限频重试

ESP-011 的 NetworkService 已实现硬件无关的非阻塞连接调度、超时、重连间隔及连接／IP 变化事件。当前使用模拟 NetworkPort 验证，尚未调用实际 Wi-Fi 库或连接真实路由器。

## 接口契约

NetworkPort 的 begin 发起一次连接尝试，立即返回是否成功发起；status 返回 Disconnected、Connecting 或 Connected。适配器必须将“尝试仍在进行”映射为 Connecting，不能直接把硬件库的暂时未联网状态一律当成连接失败。Connected 必须表示已有可用的 DHCP／静态 IP；address 只返回实际数字 IP，不返回 SSID、密码或未经处理的诊断文本。

所有方法必须快速返回，不能在 begin 中循环等待连接。disconnect 取消尝试，并禁用或协调底层自动重连，确保只有 NetworkService 决定重试节奏。该服务应独占其驱动会话，不与其他任务交叉调用 begin/disconnect。

## 调度与诊断

默认连接超时 10000 ms，失败后等待 5000 ms 再尝试；这是可配置的工程默认值，尚未经实物网络验证。参数必须大于 0 且小于 2³¹ ms，非法值只报告一次 InvalidTiming，且不触碰驱动。

首次 update 发起连接；连接中逐轮检查一次状态。异步失败、启动失败、缺失 IP 或超时会取消当前连接尝试并开始冷却。超时边界优先于迟到的连接成功，避免让超时操作重新生效。重试间隔从失败／断开时刻计算；长时间调度延迟不会补发多次重连。

已连接时报告 Connected，地址改变时报 AddressChanged，断开后报告 Disconnected 并清空旧 IP。状态不变时不重复报告诊断；上层仅在 diagnostic 非 None 时记录日志。默认时钟为单调 32 位毫秒计数，支持自然回绕，调用间隔不得跨过整个计数周期。

NetworkUpdate 每轮提供当前 connected 和 address；诊断没有 SSID／密码字段。它不能保证外部驱动自己不会打印凭据，真实适配器还需单独审查日志。

## 控制器与 HTTP 接入

App 每轮调用 networkService.update(nowMs)，将 connected 传给 FanController::setWifiConnected。这个操作只修改网络状态与 revision，不调用电机输出、不改变手动／自动模式，也不修改命令来源。连接等待期间主循环继续处理按键、红外与采集。

温度健康时钟仍须独立推进；断网保持自动控制不意味着忽略过期温度。温度失效时仍按既有规则关闭并退出自动。

连接或 IP 改变后，App 将受信任的设备地址传给 HTTP 写策略，用它构建实际可用的 origin 列表；不要继续保留已经失效的地址，也不要用客户端 Host 反向建立信任列表。此处尚未装配 HTTP 服务或写策略更新。

## 私有配置

`firmware/config/wifi.example.h` 提供 SSID／密码占位符。实际板端适配器建立后，将它复制为同目录 wifi.local.h，填写本地值；该文件已加入 Git 忽略。当前测试和模拟器不会加载该配置，仓库中没有真实凭据。

Wi-Fi 参数由板端适配器读取并交给无线库，不进入 NetworkService、公开状态 JSON 或网络事件。不要把本地配置头包含到网页资源或响应模块中。

## 验证与剩余工作

运行 `sh firmware/test/run-host-tests.sh`。NetworkServiceTest.cpp 使用模拟驱动和真实控制器，验证等待期间本地按键、连接状态推进、稳定状态不刷版本、IP 变化、断网保持模式／输出、错误凭据式失败的限频重试、精确超时和重试边界、计时回绕、缺失 IP、非法配置及无补发风暴。

正确／错误真实凭据、路由器断电与恢复、Wi-Fi 库非阻塞行为、实际 IP 输出、密码不进入驱动日志以及无线洪泛下的按键响应，均待工具链和硬件联调。ESP-011 保持进行中，ESP-008 主循环依赖不解除。
