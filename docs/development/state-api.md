# C++ 状态响应与网页契约

ESP-012 已实现独立于 HTTP 库的状态响应模块 `firmware/src/adapters/StateApi.h/.cpp`。它把控制器快照转为网页能读取的 JSON，并返回状态码及响应头要求；当前不监听网络端口，也不表示 ESP32 HTTP 服务已经可用。

## 调用边界

App 按既定顺序完成采集健康更新、命令处理和输出决策后取 `snapshot()`，将快照交给 `handleStateRequest(method, path, state)`。方法与路径由未来 HTTP 适配器解析，路径不含 query 或 fragment。该函数只读快照，不修改模式、输出或 revision，也不调用 GPIO。

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

- 输出架构规定的全部 12 个字段，模式、来源、传感器状态和故障枚举严格映射。
- 无当前有效温度时 `temperatureC` 为 `null`；从未有有效样本时 `sampleAgeMs` 为 `null`。样本失效后仍可报告距最后有效样本的年龄。
- 有效温度必须有限，健康样本须具有温度、采样记录且年龄不超过 5000 ms；不一致状态返回 503。不可用温度的内部占位数值被忽略，不以 0℃ 冒充样本。
- 阈值通过已有配置校验。revision 最大为 9007199254740991，超出时返回 503，绝不截断或四舍五入。
- bootId 要求非空、最多 128 字节 ASCII，适合后续生成 UUID／十六进制标识；引号、反斜线及控制字符正确转义。不接收未经校验的非 ASCII 字节。
- 数字输出使用固定经典 locale，保留 double 往返精度，不受系统小数逗号设置影响。JSON body 最大 2048 字节，超过返回 503。

当前使用标准 C++ 字符串流，主机以 C++11、严格警告、关闭异常及 RTTI 编译通过；板端代码体积、RAM 占用及标准库支持还需实际构建评估。这些字段上限只限制已解析输入与输出，不替代 HTTP 接收层的头部／正文大小限制、读超时或主循环处理预算。那些限制仍在 ESP-012 板端装配时实现。

## 跨语言验证

```sh
node --test firmware/test/StateApiTest.mjs
# 或全部主机测试
sh firmware/test/run-host-tests.sh
```

测试编译并运行 C++ 状态响应入口，以 Node JSON.parse 解析真实输出，再使用生产网页的 `validState` 校验。覆盖无样本、有效样本、无效／过期故障、年龄保留、字符串转义、安全整数边界、未知枚举、非法状态与配置、无缓存响应头、405／404 和长度限制。有效温度场景还在小数逗号 locale 下生成响应，验证仍输出正确 JSON 数字。

ESP-012 保持进行中：真实 GET 路由、网络处理预算、串口与实际输出一致性、本地按键在请求压力下的响应均待设备联调。已有本机 Node 模拟服务器仍是独立模拟器，不替代该 C++ 模块的板端接入。
