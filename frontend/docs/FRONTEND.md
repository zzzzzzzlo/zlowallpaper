# ZloWallpaper 前端开发与交付说明

版本 0.2.0，2026-10-09。独立 Qt/C++ Windows 客户端，基于用户提供的 Asterol 成品源码，不使用旧 deskfantasting 源码作为业务基线。本机第三方二进制取自既有可用运行包。

## 本期已实现

- 全部原桌面能力及壁纸播放链。
- 独立应用名、设置、日志数据目录、单实例锁与开机启动键。
- Dock 使用独立 `ZloWallpaperDock` 名称和配置目录；vendor/icontra 是实际运行包源文件并附有隔离修改。
- 商城分类/搜索/分页、详情与封面加载、账号注册/登录/退出、自动刷新及 DPAPI 安全记住登录。
- 订单创建、免费领取、显式测试付款、已购同步。
- 最大 3 个异步下载、取消、重试入口、授权期限、大小/SHA-256 校验、原子保存、后台缩略图生成、事务式入库、下载后应用。
- 真实接口与演示环境可切换。真实后端尚未完成；不是线上商城。

## 模块分工

```
ui/StorePage → store/StoreService → store/ApiClient → HTTP API
                     ↓ 下载授权
              store/DownloadManager
                     ↓ 文件校验、后台缩略图
              library/WallpaperLibrary
                     ↓ MainWindow::apply
              WallpaperEngine → WallpaperSurface / DesktopHost
```

`DemoServer` 是仅监听 127.0.0.1 随机端口的进程内 HTTP 夹具，客户端依然走相同 ApiClient/下载路径，便于无后端调试。演示图片由程序生成，演示用户、订单和权益在进程结束后丢失。

## 源码与部署位置

- 仓库：`https://github.com/zzzzzzzlo/zlowallpaper`
- 本机源码：`D:\Codex\software\zlowallpaper\frontend`
- 可运行部署：`E:\zlowallpaper\ZloWallpaper.exe`
- API 契约：本目录 `openapi.yaml`
- 后端需求：本目录 `BACKEND-HANDOFF.md`

二进制、Qt DLL、mpv、ffmpeg、Electron 运行包不提交 Git。整个 frontend 源码及 vendor/icontra 源码均提交。

## 运行

双击 ZloWallpaper.exe 默认为真实接口模式，默认地址 `http://127.0.0.1:8080/api/v1`。后端尚未启动时显示连接错误，可点击“进入演示模式”。

也可以执行：

```powershell
E:\zlowallpaper\ZloWallpaper.exe --demo
E:\zlowallpaper\ZloWallpaper.exe --api http://127.0.0.1:8080/api/v1
```

演示账号 `demo` / `Demo12345`。可以注册其他临时测试账号。选择免费壁纸 → 免费领取 → 下载到本地库 → 应用；收费样例创建订单后确认“测试订单（不扣款）”。

新下载的壁纸保存到程序旁的 `wallpapers/`，本次部署即 `E:\zlowallpaper\wallpapers`，下面仍按 API 地址和 userId 的 SHA-256 分账号目录。不再把新壁纸原文件放到 C 盘用户数据目录；库记录、账号凭证、缩略图与视频代理缓存仍在 Qt AppLocalDataLocation（Windows 通常 `%LOCALAPPDATA%\ZloWallpaper\ZloWallpaper`）。设置在 `HKCU\Software\ZloWallpaper\ZloWallpaper`。不会迁移或修改 Asterol/LightWallpaper 设置或库。

此前已下载的文件保留原位置，现有库记录继续有效，不自动搬迁或删除。同账号同版本已有文件仍复用；只有新的下载使用新目录。程序目录必须可写，下载目录无法创建时会报错，不默默退回 C 盘。以后搬动程序目录时，要一起保留 wallpapers 文件夹；现有库使用绝对路径，移动文件后需要重新定位。

两个客户端可以各自启动，但 Windows 桌面壁纸、透明任务栏和桌面图标可见性属于系统共享资源；两边同时控制会竞争。手工验收桌面效果时应只让一个客户端播放/控制任务栏。应用配置独立不代表操作系统桌面状态独立。

## 构建

依赖 CMake >= 3.21、C++17、Qt >= 6.8（Core、Gui、Widgets、Multimedia、MultimediaWidgets、Network、Concurrent、Test），与 libmpv 同架构的编译器。交付版使用本机 MSYS2 MinGW64 工具链。

本地 `deps/`（被 .gitignore 忽略）结构：

```
deps/
  mpv/package/include/mpv/client.h
  mpv/package/libmpv.dll.a
  mpv/package/libmpv-2.dll
  ffmpeg/package/ffmpeg.exe
  icontra/Icontra.exe
  icontra/resources/app.asar
  translucenttb/TranslucentTB.exe
  qt-imageformats/.PKGINFO
  qt-imageformats/mingw64/share/qt6/plugins/imageformats/qwebp.dll
```

可以传 `-DZLO_RUNTIME_ROOT=其他依赖根目录`。不要混用 MSVC 的 Qt DLL 和 MinGW 的 mpv 导入库。

WebP 需要与所用 Qt 版本匹配的 imageformats 插件；优先读取已安装的工具链插件，否则从 deps/qt-imageformats 获取。此目录是解压后的官方 MSYS2 Qt 插件包，版本及 SHA 见 RUNTIME-NOTICES.md。Build.ps1 在运行测试前复制插件到 build/imageformats，并在部署时带上依赖。只运行原始 cmake/ctest 时要先手动准备这一插件。

```powershell
$env:PATH = 'D:\MYSY2\mingw64\bin;' + $env:PATH
cmake -S frontend -B frontend/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=D:/MYSY2/mingw64/bin/g++.exe -DQt6_DIR=D:/MYSY2/mingw64/lib/cmake/Qt6
cmake --build frontend/build --parallel 4
ctest --test-dir frontend/build --output-on-failure
```

Dock 二次打包：对 vendor/icontra 运行 `asar pack`，输出到 deps/icontra/resources/app.asar；不要漏掉 main.js 中 `--zlo-user-data` 的隔离修改。复现脚本见 scripts/Build.ps1（PowerShell 7），依赖准备见 scripts/Prepare-Dependencies.ps1。Build.ps1 递归收集 MSYS2 非 Qt DLL，保证不依赖开发机 PATH。交付实测 Qt 版本为 6.11.1，6.8 是最低依赖版本而非本次实际版本。

重新打包 Dock 时需要 Node.js 和 @electron/asar，Prepare-Dependencies.ps1 的 AsarCli 参数指向其 bin/asar.js。该脚本接受已解压的 mpv 包、ffmpeg.exe、Icontra 和 TranslucentTB 目录；可选 QtImageFormatsPackage 指向解压后的匹配 Qt 插件包。示例：

```powershell
./scripts/Prepare-Dependencies.ps1 -MpvPackage D:/packages/mpv -FfmpegExecutable D:/packages/ffmpeg.exe -IcontraRuntime D:/packages/icontra -TranslucentRuntime D:/packages/translucenttb -AsarCli D:/tools/node_modules/@electron/asar/bin/asar.js -QtImageFormatsPackage D:/packages/qt-imageformats
./scripts/Build.ps1 -ToolchainBin D:/MYSY2/mingw64/bin -DeployDirectory E:/zlowallpaper
```

上述 packages 是自行准备的目录示例，不代表仓库已附带二进制。构建脚本出错时停止部署，不忽略失败继续交付。

## 自动测试与验收

`tests/StoreTests.cpp` 用真实 Qt Network 和本机 HTTP 夹具测试：列表搜索、错误密码、未登录与未购买下载拒绝、免费领取、重复订单、付费测试订单、授权下载/入库/重新加载/去重、文件损坏、取消、账号切换、库保存失败回滚和 DPAPI 读写，以及真实 StorePage 的登录、领取、下载和应用按钮交互。不会修改桌面或真正购买商品。

UI 冒烟：`ZloWallpaper.exe --demo --ui-smoke --screenshot <png路径>`；只创建界面并退出，不恢复壁纸、不启动 Dock/透明任务栏。用于验证部署完整、商城加载和排版。

交付实际测试结果记录于 `VALIDATION.md`。尚未存在的真实后端和真实支付不可声明通过。

## 本次必要修改与保留事项

- 修复复制源码中的 `enablescheduleRestard_` 拼写错误为 `enabled_`，否则 Dock 模块无法编译。
- 新增 WallpaperEntry 云端标识，不把商城 DTO 混同于本地文件实体。
- 修复 addPrepared/add 的保存失败回滚，以及 remove 的保存失败回滚；商城缩略图生成后台执行。
- AtomicFile 封装 QSaveFile，先解析 Windows 包目录重定向，避免由打包 IDE 启动时临时文件与逻辑目标目录出现跨卷保存错误。
- 老本地“添加壁纸”的缩略图生成仍可能同步等待 ffmpeg；已有多屏布局和无限桌面恢复重试等历史问题本期未重构，单独安排。
- V1 没有断点续传、真实收银台和自动升级；取消/失败重新下载完整文件。
- 演示只提供图片资源，真实 GIF/视频沿用已有播放器，可通过本地导入测试；后端联调需提供实际视频商品。

## 发布与第三方来源

保留 frontend/LICENSE 及 vendor/icontra/THIRD_PARTY_NOTICES.md。部署包含 Qt、mpv、ffmpeg、Electron 与 TranslucentTB；来源、版本和本机依赖哈希见 RUNTIME-NOTICES.md 与 runtime-manifest.json。正式向公众分发前需补齐对应第三方许可证和源码提供要求；本次为本机开发调试交付，不把原 Asterol 源码中的陈旧 README 当发布事实。
