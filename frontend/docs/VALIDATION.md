# 交付验证记录

日期：2026-10-09。客户端 0.2.0，Windows x64 Release；Qt 6.11.1、GCC 16.1.0、Ninja/CMake。本记录区分自动化验证和仍需真实环境验证的部分。

## 已执行

1. 从 Asterol 成品源码建立独立 frontend，完整编译 ZloWallpaper.exe 与 StoreTests.exe。
2. 执行 scripts/Build.ps1，实际运行 CTest / StoreIntegration：QtTest 10 项通过（含 init/cleanup，8 个业务测试方法），0 失败。日志见 test-results.txt。
3. 集成测试走真实 Qt HTTP 网络与本机 DemoServer，不是直接绕过客户端的假方法调用：
   - 列表/标题搜索、错误密码、登录/退出。
   - PNG/JPEG/GIF/WebP 解码插件存在，WebP 实际保存/读取。
   - 未登录或没有权益不能下载、免费领取、重复订单。
   - 收费订单 PENDING → 测试付款 PAID → 权益 → 授权下载。
   - 校验、后台缩略图、保存库、重新加载及同版本去重。
   - 损坏文件拒绝入库、取消下载、账号切换隔离。
   - 库保存失败回滚、Windows DPAPI 凭证加密读写与删除。
   - 实际 StorePage 登录弹窗、领取/下载/应用按钮和信号；应用信号由测试接收，不修改桌面。
4. 部署到 E:\zlowallpaper。PATH 仅 Windows 目录时，运行 `--demo --ui-smoke --screenshot ...`，退出码 0，商城成功加载；查看实际截图并调整中文标题字体、背景与排版。见 storefront-preview.png。
5. OpenAPI 用 js-yaml 解析，18 个路径、76 个内部引用全部可解析。此项不是实际业务后端的契约验收。
6. 去掉新 Dock 运行包内重复附带的旧播放器，保留 Dock 的 app.asar/Electron 资源；运行包约 818 MiB（大型 mpv、FFmpeg、Electron 依赖占主要空间）。二进制不推送到 GitHub。

## 构建期间发现并处理

- 成品源码的 enablescheduleRestard_ 拼写错误会阻止编译，已在新副本中修复。
- 打包 IDE 下 Windows 用户目录跨盘重定向导致文件提交失败：通过目录句柄解析实际路径后，仍采用 QSaveFile 原子提交；下载与凭证测试通过。
- 本机 Qt 基础包未包含 WebP 解码插件，已补齐匹配版本插件，并验证 WebP 读写。
- 订单刷新后移除旧的测试付款按钮，详情异步返回不再提前解除同一商品的提交锁。

## 尚未验证 / 下一阶段验收

- 真实后端尚未开发：真实持久化账号、数据库事务、幂等、权限越权、刷新重放、管理上传等服务端能力没有实现，也没有宣称通过。
- 真实收费没有实现；开发模拟付款不等于支付渠道。生产应禁止 test-pay。
- 新程序的实体多屏、睡眠唤醒、Explorer 重启、GPU/视频代理、真实 Electron Dock 与透明任务栏的完整回归需单独手工测试。此次保留原功能源码与运行组件，不擅自重启 Explorer 或改动当前桌面来测试。
- 新版与 Asterol 数据、单实例与 Dock 配置独立，但系统桌面资源共享；手工桌面测试应避免两边同时控制壁纸/Dock/任务栏。
- 演示商品仅图片；真实视频商城下载播放要在后端联调中用实际视频商品验收。
- offscreen 测试会输出 propagateSizeHints 等平台提示，不代表业务失败；实际 Windows 界面冒烟已单独执行。
- 当前为本机开发调试交付。正式公众发行前仍须完成第三方对应源码/许可证合规、更多硬件回归和发布流程。

后端完成后应按 BACKEND-HANDOFF.md 的验收矩阵逐项联调，不能仅依据本机演示通过就宣布上线。
