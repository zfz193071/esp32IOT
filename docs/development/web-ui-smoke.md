# 温控表单浏览器验收

`firmware/test/WebUiSmoke.mjs` 使用 Chrome DevTools Protocol 在独立后台标签中运行真实网页。无第三方 Node 依赖，需要支持内置 WebSocket 的 Node.js（本轮 v22.19.0）和已启动的独立 Chrome 调试实例。脚本不使用或关闭其他标签，不激活窗口；只在退出时关闭自己创建的标签。

## 运行

先在两个独立终端启动专用服务，保持运行。不要复用日常浏览器的配置目录，不要关闭其他任务占用的端口；若示例端口已占用，换用空闲端口并设置下方环境变量。

```sh
# 终端一：必须是新启动、默认阈值的独立模拟器
ESP32_PREVIEW_PORT=18316 node firmware/tools/preview-server.mjs

# 终端二：macOS 无界面 Chrome；使用专用临时配置目录
'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' --headless=new --remote-debugging-port=19316 --user-data-dir=/private/tmp/esp32-form-check-19316 --no-first-run --no-default-browser-check about:blank

# 终端三：从仓库根目录运行
node firmware/test/WebUiSmoke.mjs
```

可选环境变量：`ESP32_UI_BASE` 指定模拟器 URL（不带末尾斜线），`ESP32_UI_CDP` 指定浏览器调试 HTTP 地址，`ESP32_UI_SCREENSHOT` 指定截图输出文件路径。默认前两者分别为 `http://127.0.0.1:18316` 和 `http://127.0.0.1:19316`。测试先检查模拟器专用响应头，拒绝直接在真实设备上运行。

测试会把专用模拟器阈值从 30/28 改为 35.5/32；重复运行前重启该模拟器以恢复初始状态。结束后停止自己启动的模拟器和 Chrome。此脚本不放入默认主机回归，因为它要求额外的 Chrome 进程与本地端口权限。

## 检查内容与边界

- 390×844 手机视口下页面无横向溢出，默认阈值和模拟标识可见。
- 30/29.5 的非法阈值组合显示校验提示，模拟器阈值不变。
- 轮询不覆盖 35.5/32 草稿；保存后页面收到确认，模拟 API 返回相同值。
- 编辑 40/38 后模拟离线：保存禁用，草稿保留；恢复后草稿仍保留，设备值仍为 35.5/32，未自动保存。
- “使用设备值”恢复最近同步的阈值，不提交编辑值。

2026-10-29：专用无界面 Chrome 完成以上检查，已归档脚本再次执行通过。另保存并查看全页截图，确认表单、按钮、提示文字在手机宽度下正常排布。默认 Chrome DevTools MCP 因共享配置目录占用无法连接，本轮使用独立 Chrome 的 CDP 完成；未影响既有会话。

这只验证本机模拟器上的浏览器行为，不代表手机实机、ESP32 HTTP、真实存储、断电恢复或传感器联调完成。此次没有修改页面业务代码。
