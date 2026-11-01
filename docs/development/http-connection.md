# HTTP 连接与有界发送

`firmware/src/adapters/HttpApiPort.*` 将增量解析器、生成网页和 AppLoop 接在一起。`adapters/NetworkHttpApi.*` 管理网络变化与监听生命周期；`board/SocketHttpTransport.*` 提供 BSD socket 驱动，真实回环 TCP 测试通过；`board/Esp32SocketCloser.*` 提供独立关闭任务并通过主机 FreeRTOS 桩验证。ESP32 工具链装配、Wi-Fi 实物联调和运行耗时仍待验证。

## 主循环接入

构建时先运行 `embed-web.mjs`，将生成目录加入 include 路径，并编译 `WebAssets.cpp`。传输端口必须比 NetworkHttpApi 活得更久，NetworkHttpApi 必须比 AppLoop 活得更久；只在同一个主循环线程调用。

```cpp
NetworkHttpApi http(transport); // 默认 HTTP 端口 80
// 创建 AppLoop 时，将 &networkService 和 &http 分别作为网络服务、ApiPort 传入。
// setup 先 app.start() 建立关闭输出，再 closer.start() 初始化关闭任务。
// 主循环只调用 AppLoop；不要另行调用 HTTP update/poll 或 NetworkService.update：
app.step(nowMs, buttonPressed);
```

AppLoop 每轮只取得一次 NetworkService 结果，用它更新 Wi-Fi 状态与可信来源，然后处理按键／红外，再调用 `ApiPort.update(nowMs, networkUpdate)`。NetworkHttpApi 在该步骤清理旧连接、推进监听或调用一次内部 `HttpApiPort.poll`，然后才允许 `takeRequest` 移交完整的 `/api/` 请求。AppLoop 先提交最终输出，再调用 `sendResponse`。响应复制到有界缓冲，下一轮才尝试发送；禁止异步保存请求后跨连接回调响应。

ApiPort.update 的默认实现为空，已有模拟 API 端口保持原有接入方式。独立 HttpApiPort 仍可用于手工管理监听的实验／测试，但必须自行在 AppLoop.step 前调用 poll，并在断网时先 stop 再停止监听；不要把该调用方式混入 NetworkHttpApi 装配。

## 网络生命周期

- 未获得地址、断网或地址为空时不监听，也不读取客户端。管理器只保存至多 15 字节的 IPv4 地址；数值格式由 SocketHttpTransport 在绑定前校验。没有 NetworkService 的 AppLoop 向管理器提供离线状态。
- 首次获得地址立即尝试 begin。创建监听器的这一轮不再 accept／读写；下一轮才推进 HTTP。
- 地址变化或断网时，先清空 HttpApiPort 的接收、待执行请求和待发送响应，再 end 监听器。下一次 takeRequest 不能交付旧连接的命令。已经执行的命令不撤销，当前输出和温控配置不会因网络变化而恢复默认值。
- 两次 begin 尝试至少相隔 5000 ms，失败后调用 end 清理部分资源。预算跨断网、重连和地址变化保留，防止连接抖动绕过限频；按 uint32 时间差处理计时回绕。刚启动监听后很快换地址，可能需要等待剩余间隔。
- 同一有效地址不反复 bind，离线期间不反复 end。旧 socket 尚未释放或关闭任务尚未就绪会使 begin 返回 false，等待下一次重试；主循环仍继续按键、红外和温度调度。
- `listening()` 表示本地 begin 已成功，不表示对端可达、正在传输或实物设备可用。查看 SocketHttpTransport.lastError／Esp32SocketCloser.lastError 可获取底层诊断，不把它们写进业务状态 JSON。

NetworkHttpApi 独占监听器生命周期；装配后不要从其他线程／主循环分支直接 begin、end 或 poll 同一个传输对象。析构时先清理 HTTP，再停止监听；关闭器仍遵循后文的销毁顺序。

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

HttpTransport 的 accept／receive／send／close 不能等待对端收发、执行无限重试、flush 或创建随客户端增长的应用队列。Progress 必须报告 1 到请求容量之间的字节数，send 必须在返回前消费或复制数据，不能保留临时指针。下面的 socket 驱动落实了这些限制；系统调用仍可能等待 SDK 内部调度／锁，单客户端设计的手机加载延迟、CPU 占用和多连接行为须在设备上测量。

`HttpApiPortTest.cpp` 在默认主机回归中编译真实生成资源、解析器、AppLoop、控制器及 API。覆盖逐字节接收、短写后的全部资源字节一致性、HEAD／405／404、时钟回绕、收发故障、排队及发送超时、响应所有权和大小／头字段限制，以及慢读者期间按键接管、先输出后响应、流水线命令不执行和下一连接状态读取。该结果不代表网络 I/O 已被证明非阻塞，也不关闭设备验收项。

## BSD socket 驱动

`SocketHttpTransport` 使用 socket／bind／listen／accept／recv／send。监听器和每个接受到的客户端都显式设置 `O_NONBLOCK`；EAGAIN、EWOULDBLOCK、EINTR 留到下一轮处理，不在一次调用里重试。底层读写也限制为 256 字节。桌面平台通过 MSG_NOSIGNAL 或 SO_NOSIGPIPE 防止连接重置触发 SIGPIPE；ESP32 lwIP 不使用 Unix 信号。

`begin(address, port)` 只接受明确的数字 IPv4 地址，不进行 DNS，不绑定 `0.0.0.0` 或多播／广播地址。设备应传入 NetworkPort 当前已获取的地址及端口 80；测试传入 `127.0.0.1` 和端口 0，由系统分配空闲端口，`port()` 返回实际端口。重复 begin 不替换活动监听器；端口冲突、选项设置或监听失败会释放候选句柄，并保留 `lastError()` 的 errno。

监听 backlog 为 2（内核实际排队行为仍由 TCP 栈决定）；应用只拥有一个监听器和一个客户端。没有完成上一客户端关闭时不接受下一客户端。`end()` 关闭监听和客户端并将 port 清零，可重复调用；它不改动控制器或持久配置。

构造次序：

```cpp
Esp32SocketCloser closer;
SocketHttpTransport transport(closer);
NetworkHttpApi http(transport);
// AppLoop 构造时传 &networkService、&http；对象按相反顺序销毁。
// setup: 先 app.start() 建立输出关闭，再 closer.start()。
// 后续只调用 app.step，由 NetworkHttpApi 根据该轮网络结果启动／停止监听。
```

上述生命周期已经由 NetworkHttpApi 实现；尚未提供未经目标工具链验证的完整烧录工程。地址比较和可信 Origin 均来自 NetworkService，不把请求 Host 当作监听地址或信任依据。设备默认端口 80 与 AppLoop 的动态 Origin 一致；测试可用端口 0，非默认端口装配需显式配置匹配端口的可信 Origin。

## ESP32 关闭任务

参考 [ESP-IDF BSD socket 支持与 fcntl](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/lwip.html#fcntl) 和 [esp-lwip 关闭实现](https://github.com/espressif/esp-lwip/blob/master/src/api/api_msg.c)：即使非阻塞 socket，在关闭时无法分配 FIN 段也可能等待。为避免在控制循环中承担这段等待，SocketCloser 接口将句柄所有权移交给专用任务；主循环不调用系统 close。

`Esp32SocketCloser.start()` 在 setup 中创建一个优先级 1、栈 3072 字节的任务，以及两个深度为 2 的小队列（任务／完成通知）。这套资源专用于一个 SocketHttpTransport，容量覆盖唯一客户端和监听器；不能与其他 socket 用户共享。创建失败会清理已申请资源并返回 false；重复成功调用不再创建任务。资源与栈大小采用 [ESP-IDF FreeRTOS API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)，仍需目标版本编译及栈余量测量。

运行期 release／idle／lastError 只执行零等待队列操作，每次最多收取两个完成结果。关闭任务卡住时，客户端／监听器总数不会继续增长，HTTP 暂停接受新连接，本地控制循环继续运行。若移交暂时被拒绝，传输层保留旧句柄并禁止继续读写，后续 accept／begin／end 重试移交。close 返回异常时记录错误，并阻止创建新网络句柄；不盲目重试可能已失效的描述符，也不自动重启设备。该异常下系统资源释放无法确认，需重启后重新检查。

正常关闭保留 TCP 默认语义，不设置零秒 SO_LINGER 来强制丢弃待发送正文。不等待响应被对端确认；在未读请求尾部、断网或重置时，TCP 响应仍可能丢失或截断。已执行命令不因此回滚，页面继续使用既有“结果未知后查询、禁止自动重发”的恢复流程。

关闭器析构是**停止控制循环后的清理步骤**：等待所有已提交关闭完成，通知任务退出后释放队列。必须让 transport 比 closer 先销毁，不能在运行中的主循环临时创建／销毁关闭器。运行期断网使用 end，不析构关闭器。未开始或未成功开始的关闭器不会允许传输层创建监听器。

## 真实 TCP 回归

```sh
sh firmware/test/run-socket-tests.sh
# 或连同默认回归执行：
sh firmware/test/run-host-tests.sh --with-sockets
```

仅监听随机的 `127.0.0.1` 端口，不读取 Wi-Fi 凭据、不访问设备、不接管已有进程，退出后删除自己的临时产物并释放 socket。需要操作系统允许本机监听；默认测试入口编译 socket 驱动并执行关闭任务桩测试，但明确跳过 TCP 测试。

`SocketHttpTransportTest.cpp` 使用真实 BSD socket、生成资源、解析器、NetworkHttpApi 和 AppLoop，验证全部静态正文、HEAD、分段请求、开关与状态读取、跨源拒绝、截断半关闭、空闲客户端期间按键推进、端口占用、非阻塞读取、驱动大小上限、关闭任务反压及模拟无线断网／同 IP 重连后的自动监听恢复。`Esp32SocketCloserTest.cpp` 使用主机线程和队列桩，验证关闭不在控制线程发生、停滞时队列有界、申请失败可重试、错误锁定和析构清理。

`NetworkHttpApiTest.cpp` 在默认回归中用模拟监听器验证地址变化、失败清理、跨断网限频、时钟回绕、各请求阶段清理、待发响应丢弃，以及真实 NetworkService／AppLoop 下的可信地址更新、按键和采集推进。该测试检查每个已启动主循环只观察一次网络状态，避免板端额外 update 造成同轮状态不一致。

本机网络验证不能替代 ESP32 的 lwIP、FreeRTOS、无线缓冲和驱动计时验证；没有执行设备编译、烧录或实物验收。
