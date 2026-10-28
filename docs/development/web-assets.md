# 将网页编入 C++ 固件

ESP-014 的资源生成步骤已实现：将五个网页文件转成只读 C++ 字节数组、路由表和校验清单。主机可编译并逐字节核验生成结果；ESP32 构建钩子、HTTP 发送适配和 Flash 布局仍待实际工具链确认。

## 生成与验证

```sh
node firmware/tools/embed-web.mjs
node --test firmware/test/WebAssetsTest.mjs
```

默认输出至被 Git 忽略的 `firmware/build/generated/`，也可通过第一个参数指定输出目录。生成文件为 `WebAssets.h`、`WebAssets.cpp`、`web-manifest.json`。修改网页后必须重新生成，再编译固件；生成失败时构建必须停止，不能继续使用旧资源。

脚本只读固定的五个文件，不递归扫描目录，不包含模拟服务器或私有配置；先读全所有源文件，再开始写出。相同输入产生相同输出，无构建时间戳。清单记录每个路由的源文件、MIME、字节数和 SHA-256。

## C++ 接入契约

构建系统应将生成目录加入头文件搜索路径，将 `WebAssets.cpp` 编译到固件中，且每个目标仅编译一份。命名空间 `esp32fan::web` 提供 `findAsset(path)`、`assets` 和 `assetCount`。

`findAsset` 使用精确路径匹配，不存在或传入空指针时返回空指针。HTTP 适配器负责解析 URL、限定 GET／HEAD、处理未知路由，以及根据 `Asset::contentType`、`Asset::data`、`Asset::size` 返回响应。数据不是以 NUL 结尾的字符串，必须按明确字节长度发送，不能调用 `strlen` 或无长度字符串重载。

资源为标准 C++ `const unsigned char` 数组，没有绑定 Arduino 库和板型。它们在实际 Flash／RAM 中的放置、HTTP 库读取方式及固件分区容量需在板端构建后核对，不依据主机编译推断。当前不提供未经验证的 `send_P` 或 PROGMEM 装配。

路由为 `/`、`/style.css`、`/app.mjs`、`/client.mjs`、`/config.mjs`，JS 模块使用 `text/javascript`。模拟服务器直接读源文件；它不使用生成资源，所以仅看本机预览不能验证固件资源是否更新。

## 测试范围

`WebAssetsTest.mjs` 在临时目录重复生成，检查可重复性，再以 C++11 严格警告且关闭异常／RTTI 编译生成代码与测试入口。测试通过实际 C++ 查找接口输出所有路由的数据，与原文件逐字节比较，验证中文 UTF-8、长度、SHA-256、JS MIME 及未知／空路径拒绝，结束后清理临时产物。

该测试加入统一主机测试入口，仍不能替代真实 ESP32 编译和手机访问设备 IP 的验证。
