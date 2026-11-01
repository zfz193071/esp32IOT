# C++ 状态响应与网页契约

ESP-012 已实现独立于 HTTP 库的状态响应模块 `firmware/src/adapters/StateApi.h/.cpp`。它把控制器快照转为网页能读取的 JSON，并返回状态码及响应头要求；当前不监听网络端口，也不表示 ESP32 HTTP 服务已经可用。

## 调用边界

App 按既定顺序完成采集健康更新、命令处理和输出决策后取 `snapshot()`，将快照交给 `handleStateRequest(method, path, state)`。方法与路径可由下述增量解析器提供，路径不含 query 或 fragment。该函数只读快照，不修改模式、输出或 revision，也不调用 GPIO。

返回值 `StateResponse` 包含 status、contentType、cacheControl、allow 和 body。HTTP 适配器必须实际发送这些值，不能只发送 body；`allow` 非空时设置 `Allow` 响应头。

| 请求／状态 | 响应 |
|---|---|
| `GET /api/v1/state` 且状态有效 | 200，完整状态 JSON |
| 已知路径使用其他方法（包括 HEAD） | 405，`Allow: GET` |
| 未知路径 | 404，不回显路径内容 |
| 已解析路径超过 128 字节 | 414 |
| 方法名称超过 16 字节 | 400 |
| 状态无法安全序列化 | 503，`STATE_UNAVAILABLE` |

所有响应都要求 `Content-Type: application/json; charset=utf-8` 与 `Cache-Control: no-store`。错误格式为 `{"error":{"code":"...","message":"..."}}`，消息为固定文本，不包含请求内容、私有配置或底层异常。

## 序列化规则

- 输出架构规定的全部 13 个字段，模式、来源、传感器状态和故障枚举严格映射。
- `configSaveBlocked` 为布尔值，表示配置保存因未知结果暂停。首次置 true 时 revision 加 1；同次启动不会解除，重启后重新读取存储并恢复 false。它不表示风扇停止，也不改变温控故障枚举。网页与状态接口应配套发布，缺少此字段的旧响应会被新版网页拒绝。
- 无当前有效温度时 `temperatureC` 为 `null`；从未有有效样本时 `sampleAgeMs` 为 `null`。样本失效后仍可报告距最后有效样本的年龄。
- 有效温度必须有限，健康样本须具有温度、采样记录且年龄不超过 5000 ms；不一致状态返回 503。不可用温度的内部占位数值被忽略，不以 0℃ 冒充样本。
- 自动模式必须具备健康样本且没有温控故障；健康样本不能同时标记温控故障，与网页校验一致。不一致状态返回 503。传感器故障后仍允许手动开启，因此手动模式、无效温度、温控故障与开启输出的组合是有效快照。
- 阈值通过已有配置校验。revision 最大为 9007199254740991，超出时返回 503，绝不截断或四舍五入。
- bootId 要求非空、最多 128 字节 ASCII，适合后续生成 UUID／十六进制标识；引号、反斜线及控制字符正确转义。不接收未经校验的非 ASCII 字节。
- 数字输出使用固定经典 locale，保留 double 往返精度，不受系统小数逗号设置影响。JSON body 最大 2048 字节，超过返回 503。

当前使用标准 C++ 字符串流，主机以 C++11、严格警告、关闭异常及 RTTI 编译通过；板端代码体积、RAM 占用及标准库支持还需实际构建评估。StateApi 的字段限制只处理已解析输入与输出；接收限制由下面的解析器提供，实际 socket 读写预算仍需板端适配实现。

## HTTP 请求增量解析

`firmware/src/adapters/HttpRequestParser.*` 接收原始字节，不访问网络或控制器。它实现直接局域网浏览器请求所需的受限 HTTP/1.1 格式；不是通用 HTTP 服务器。仅接收以 `/` 开始的请求目标、ASCII 请求行／头部和 CRLF 换行；Host 支持 DNS／IPv4 名称及可选十进制端口。绝对 URL、代理、IPv6 authority、chunked、Expect 和 HTTP/1.0／2 不在本实现范围内。正文保留原始字节，JSON 校验交给 WriteApi。

| 限制 | 当前实现值／行为 |
|---|---|
| 每次 `feed` | 最多消费 256 字节，返回实际消费量；主循环每轮至多调用一次 |
| 接收期限 | 从接受连接起累计 2000 ms，收零星字节不会续期，支持 uint32 时钟回绕 |
| 头部总量 | 请求行、各头及空行的 CRLF 全部计入 2048 字节上限 |
| 单行 | 不含 CRLF 最多 512 字节 |
| 方法／路径 | 16／128 字节；query 不传给路由，路径不做百分号解码或规范化 |
| Host／Origin／Content-Type | 128／256／128 字节，名称大小写不敏感，值只去除两端空格和制表符 |
| 正文 | Content-Length 指定，最多 1024 字节；PUT 必须带长度，声明超限即拒绝 |
| 请求交付 | 只有收齐后 `takeRequest` 才成功，且仅一次；失败不修改调用方输出 |

缺少／重复 Host，重复 Origin、Content-Type、Content-Length（即使值相同），非法长度、折行、冒号前空白、控制字节和 Transfer-Encoding 一律拒绝。Host 是否可信、Origin 是否同源仍由 WritePolicy 决定；空 Origin 保留为“存在且为空”，不得当作无 Origin。分帧和重复 Host 规则参照 [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)，本项目采用更窄的协议子集。

解析失败由 `errorStatus()` 返回 400；超时 408、PUT 缺长度 411、正文超限 413、路径／请求行超限 414、Expect 417、头部超限 431、非 HTTP/1.1 版本 505。解析器不生成网络响应；socket 适配器负责发送固定错误体、`Cache-Control: no-store` 和 `Connection: close`，随后关闭连接。不得回显原始输入。接收阶段 EOF 调用 `endOfInput()`，未收齐时拒绝而不执行命令。

每条连接只交付一个请求。解析完成后停止消费，剩余流水线字节须在关闭连接时丢弃；不能在同一连接重建解析器继续执行第二条命令。接受新连接时用接受时刻创建新解析器，每轮即使无数据也调用 `tick(nowMs)`；没有完整请求时继续按键、红外、温度与网络调度。[HttpApiPort](http-connection.md) 已实现单客户端、响应分段发送、静态资源路由及 AppLoop 接入，并通过模拟传输端口测试。真实 TCP 监听和 ESP32 socket 驱动仍待实现，当前不宣称设备 HTTP 已可用。

`HttpRequestParserTest.cpp` 验证每个双片切分位置、逐字节输入、重复／冲突头、长度与总量边界、慢速发送、时钟回绕、逐位置截断、一次性交付及原始请求到真实 WriteApi／FanController／ConfigStore 的串接。测试中的接收来自内存，不能替代真实 socket 的耗时和本地按键响应验收。

## 跨语言验证

```sh
node --test firmware/test/StateApiTest.mjs
# 或全部主机测试
sh firmware/test/run-host-tests.sh
```

测试编译并运行 C++ 状态响应入口，以 Node JSON.parse 解析真实输出，再使用生产网页的 `validState` 校验。覆盖无样本、有效样本、无效／过期故障、年龄保留、字符串转义、安全整数边界、未知枚举、非法状态与配置、无缓存响应头、405／404 和长度限制。有效温度场景还在小数逗号 locale 下生成响应，验证仍输出正确 JSON 数字。

ESP-012 保持进行中：真实 GET 路由、网络处理预算、串口与实际输出一致性、本地按键在请求压力下的响应均待设备联调。已有本机 Node 模拟服务器仍是独立模拟器，不替代该 C++ 模块的板端接入。
