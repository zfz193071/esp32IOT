# HTTP 连接与有界发送

`firmware/src/adapters/HttpApiPort.*` 将增量解析器、生成网页和 AppLoop 接在一起。它实现 `ApiPort`，通过 `HttpTransport` 使用一个已接受的客户端。当前测试使用内存传输端口；真实 TCP 监听、ESP32 socket 驱动和工具链装配尚未实现。

## 主循环接入

构建时先运行 `embed-web.mjs`，将生成目录加入 include 路径，并编译 `WebAssets.cpp`。传输端口必须比 HttpApiPort 活得更久，HttpApiPort 必须比 AppLoop 活得更久；只在同一个主循环线程调用。

```cpp
HttpApiPort http(transport);
// 创建 AppLoop 时，将 &http 作为 ApiPort 传入，然后调用 app.start()。
// 每轮使用同一个单调时钟样本：
http.poll(nowMs);
app.step(nowMs, buttonPressed);
```

`poll` 先推进连接；`takeRequest` 只移交完整的 `/api/` 请求且仅一次。AppLoop 按既定顺序处理采集、按键、红外和 API，先提交最终输出，再调用 `sendResponse`。响应复制到有界缓冲，下一轮 poll 才尝试发送；禁止异步保存请求后跨连接回调响应。若网络关闭，应先 `http.stop()` 丢弃当前请求／响应，再关闭底层监听器。重新开放监听由调用方负责。

## 预算与生命周期

| 项目 | 限制 |
|---|---|
| 应用层客户端 | 1 个，无等待请求队列；处理完或断开后才尝试接受下一个 |
| 每轮 I/O | 至多一次 accept、receive 或 send；读／写至多 256 字节 |
| 接收总期限 | 自 accept 起 2000 ms，无数据也计时，见解析器限制 |
| 响应总期限 | 请求完整／解析被拒绝起 5000 ms，包含等待 AppLoop 和排队发送；短写不续期 |
| API 响应正文 | 至多 2048 字节，超限转为固定 500 错误 |
| 应用响应头字段 | Content-Type 128、Cache-Control 64、Allow 32 字节；拒绝控制字符与非 ASCII 字节 |
| 静态正文 | 直接引用生成数组、按长度分段发送，不复制整份网页到响应字符串 |

发送偏移只按传输端确认接受的字节数增加；WouldBlock 留待下一轮；错误、响应超时或完整发送后释放客户端。接收 EOF 未形成完整请求时返回 400；解析错误不回显输入。uint32 毫秒差处理时钟回绕。已执行的控制命令不会因为响应发送失败而撤销，也不重发；页面按既有未知结果恢复流程读取状态。

每条连接仅处理一个请求，响应带 `Connection: close`。同次读取中第一条请求后的流水线字节直接丢弃，后续网络缓冲不得再解析为命令。`stop()` 可重复调用，并清除未交付请求与响应。

## 路由与响应

- `/api/` 前缀交给 AppLoop；API 的原有 404／405、Allow、JSON 错误码和同源策略保持有效。
- 五个生成资源精确匹配，支持 GET／HEAD；HEAD 返回相同资源长度但不发送正文；其他方法返回 405 与 `Allow: GET, HEAD`。未知静态路径返回 404。
- 所有现有响应均带 `Cache-Control: no-store`，使用明确的 Content-Length，无 chunked、keep-alive 或开放 CORS。
- 完整 HEAD 请求的 API 错误响应也不发送正文。传输层错误为固定 `{"error":{"code":"HTTP_ERROR","message":"..."}}`，其中 message 为内部状态描述。

格式依据 [RFC 9110 的 HEAD 语义](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.2) 与 [RFC 9112 的消息长度规则](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)。这仍是项目所需的有限 HTTP/1.1 子集。

## 底层实现要求与验证边界

HttpTransport 的 accept／receive／send／close 必须立即返回，不能隐藏阻塞等待、无限重试、flush 或随客户端增长的应用队列。Progress 必须报告 1 到请求容量之间的字节数，send 必须在返回前消费或复制数据，不能保留临时指针。底层还需明确有限监听 backlog、Wi-Fi 生命周期、关闭时未读 TCP 数据与已排队发送数据的处理；这些不能由当前内存测试证明。单客户端设计的手机加载延迟与多连接行为须在设备上测量。

`HttpApiPortTest.cpp` 在默认主机回归中编译真实生成资源、解析器、AppLoop、控制器及 API。覆盖逐字节接收、短写后的全部资源字节一致性、HEAD／405／404、时钟回绕、收发故障、排队及发送超时、响应所有权和大小／头字段限制，以及慢读者期间按键接管、先输出后响应、流水线命令不执行和下一连接状态读取。该结果不代表网络 I/O 已被证明非阻塞，也不关闭设备验收项。
