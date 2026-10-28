# C++ 写请求与控制器接入

ESP-013 的开关／模式处理，以及 ESP-020 的配置写入处理，已在 `firmware/src/adapters/WriteApi.h/.cpp` 实现。它们接收 HTTP 适配器解析后的请求，实际调用 FanController 和 ConfigStore，不再仅是 Node 模拟行为。当前模块没有网络监听，板端 HTTP 库、真实存储和主循环装配仍待完成。

## 三个入口

| 入口 | 正文 | 处理 |
|---|---|---|
| `PUT /api/v1/power` | `{"on":true}` 或 `{"on":false}` | 显式开关并切换手动，来源 web |
| `PUT /api/v1/mode` | `{"mode":"manual"}` 或 `{"mode":"auto"}` | 手动保持输出；自动由控制器检查有效温度并计算 |
| `PUT /api/v1/config` | `{"onThresholdC":30,"offThresholdC":28}` | 完整阈值对校验、保存成功后发布，新阈值触发自动重算 |

成功返回 200 和 StateApi 生成的完整快照。400 为参数错误，403 为来源拒绝，409／`TEMPERATURE_UNAVAILABLE` 为无法进入自动模式，503／`STORAGE_FAILURE` 为配置保存失败。未知路径 404，其他方法 405 且 `Allow: PUT`；GET 不会执行写入。所有响应均要求 `Cache-Control: no-store`，不输出开放 CORS 头。

在执行写入前检查当前状态可被安全序列化，并预留一次 revision 增量；当前版本已到 JavaScript 安全整数上限时拒绝写入，避免修改后无法提供可信结果。

## JSON 与请求上限

解析器专门处理这三个入口的扁平命令对象，不是通用 JSON 库。它接受字段任意顺序、JSON 空白、数值小数／指数形式、ASCII 字符转义；所有有效字段名和模式值都是 ASCII。

拒绝空对象、缺字段、多余字段、重复字段（含转义后相同的键）、错误类型、未知枚举、数组、嵌套对象、null、注释、尾随逗号和尾随数据。数字遵守 JSON 语法，不允许前导加号、前导多余零、NaN／Infinity 或未完成的小数／指数；阈值继续经过已有 0～60℃ 和至少 1℃ 回差校验。

正文最多 1024 字节，路径最多 128 字节，Host 最多 128 字节，Origin 最多 256 字节，Content-Type 最多 128 字节；解码后的键／字符串值与数字 token 最多 64 字节。接受 `application/json` 和可选的 UTF-8 charset 参数，媒体类型大小写不敏感。正文超限 413，路径超限 414。

这些上限针对已收到的请求；板端 HTTP 层必须在分配大缓冲前执行接收上限、超时和处理预算，不能先无限读取再调用本模块。后续若采用成熟 JSON 库，仍必须保留上述严格字段和重复键拒绝规则。

## 同源策略

`WritePolicy::allowedOrigins` 由设备配置提供完整可信 HTTP origin，例如 `http://192.168.1.20` 或 `http://fan.local:8080`，不带尾斜杠。此列表不能由传入的 Host 动态建立。Host 必须精确匹配某个已配置 authority，Origin 必须精确匹配同一 origin，包括端口。

只允许匹配列表的同源写入；外部 origin、`null` origin、存在但为空的 origin、攻击者提供的匹配 Host／Origin 均被拒绝。地址变化时应由 NetworkService 更新可信配置，不放宽为任意 Host。当前仅实现项目局域网 HTTP 场景，HTTPS 代理接入需要重新定义可信边界。

没有 Origin 的诊断请求默认拒绝。仅在明确配置 `allowOriginlessDiagnostics=true` 时允许，且仍须匹配可信 Host。这一例外针对 CLI 调试，不是身份认证；可信局域网之外不应使用此接口策略。

HTTP 适配器必须拒绝重复的 Host、Origin、Content-Type 以及冲突的 Content-Length／Transfer-Encoding，或将 `ambiguousHeaders` 标为 true；不能随意挑一个重复头值传入。Header 大小写、URL path/query 分离和消息分帧都属于传输层职责，本模块不解析原始 TCP 字节。

## 装配顺序与验证

主循环先提交新采集结果、推进健康检查，再按接收顺序调用写处理器，最后提交最终控制输出。所有入口共享同一 FanController 与已加载的 ConfigStore。调用应串行执行，不从 HTTP 回调并发修改控制器。

`firmware/test/WriteApiTest.cpp` 使用真实 C++ 控制器、状态序列化和配置流程，只有底层持久化替换为故障注入实现。运行 `sh firmware/test/run-host-tests.sh` 可验证：显式命令幂等、严格 JSON、跨源与端口规则、无 Origin 策略、请求边界、自动准入、手动接管、配置保存及自动重算、存储失败后完整状态不变。错误响应不会回显敏感请求。

这些主机验证不等于真实浏览器对板端 HTTP 的联调，或真实 Flash 的原子提交验证。ESP-013／020 均保持进行中，等待依赖工单和设备装配。
