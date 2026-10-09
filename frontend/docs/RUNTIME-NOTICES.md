# 第三方运行包与发布边界

本交付为本机开发调试包，不等同于已完成合规审核的公众发行版。
源代码许可证保留原作者 MIT 声明；附带 GPL 组件的整包发行义务不能因此忽略。

| 组件 | 本次来源与版本 | 说明 |
|---|---|---|
| Qt | 本机 MSYS2 MinGW64 Qt 6.11.1，动态链接 | 基础、Multimedia、Network 等模块；licenses/qt6-* 保存本机许可证文本 |
| Qt WebP 插件 | 官方 MSYS2 mingw-w64-x86_64-qt6-imageformats 6.11.1-1 | [固定包](https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-qt6-imageformats-6.11.1-1-any.pkg.tar.zst)，SHA-256 f3dfe8ee3217ff068193213ff5d9e9b950727c77b69e62550ca22eb1d89cb281；只部署 qwebp 与其所需 DLL |
| MinGW | 本机 GCC 16.1.0 / MSYS2 mingw64 | 编译器运行库，许可证见 licenses/gcc-libs |
| libmpv | 既有 Asterol 运行依赖；原依赖说明钉住 mpv-dev-x86_64-20260531-git-13a3e3a.7z | 来源 [shinchiro 构建](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/)；GPL-2.0-or-later，实际二进制 SHA 以 manifest 为准 |
| FFmpeg | 既有运行包；N-125658-g0869e710e6-20260718，GCC 15.2.0 | [BtbN 构建](https://github.com/BtbN/FFmpeg-Builds/releases)，启用 GPL/version3 编码组件；不是 LGPL-only 构建 |
| Dock | 本机既有 icontra-runtime-v10，app.asar 元数据 0.5.1 | vendor/icontra 是提取后修改的实际应用源文件；Electron/Chromium 通知在 icontra 目录；新增独立 userData，不改变 IPC 协议 |
| TranslucentTB | 本机既有可运行包，exe 产品版本 2026.1.0.322e2b7 | [项目源代码](https://github.com/TranslucentTB/TranslucentTB)；版本读取自 Windows 文件资源，不根据目录名推断 |

`runtime-manifest.json` 由 Build.ps1 生成，记录本次部署中 EXE、DLL 和 asar 的大小及 SHA-256。它是交付指纹，不是完整软件供应链认证、源码归档或安全审计。

部署不包含 Dock 资源目录里重复附带的旧 LightWallpaper 播放器副本；原始依赖和原 Asterol 不受影响。Dock 所需 Electron 资源和 app.asar 均保留。

依赖不进 Git。Prepare-Dependencies.ps1 接受已解压、可信来源的包并重打包 Dock；不会自动下载“最新版本”掩盖 ABI/许可变化。后续换依赖要重新构建、验收和更新 manifest。

正式公开分发前，必须补齐 mpv、FFmpeg、TranslucentTB 及所有打包库的对应许可证、版权通知、所需对应源码或源码提供方式，并确认组合授权条件。当前 sources/vendor 与许可证文本不能代替所有 GPL 对应源码。MIT 的客户端源码与附带运行包应分别说明，不能将整体宣传为纯 MIT。

## 文件提交兼容处理

由打包版 Codex 启动测试子进程时，Windows 将逻辑 C: AppData 重定向到 D: 的包缓存。诊断 GetFinalPathNameByHandle 返回实际 D: 路径，QSaveFile 的原生 rename 报跨卷错误。此问题不能归结为所有 Qt 用户都会遇到的缺陷。
AtomicFile 是 QSaveFile 的薄封装：使用目录句柄先解析实际目标目录，再由 QSaveFile 完成同目录临时写入和原子提交。仍然不启用直接写入降级或跨卷复制。
参考 [Qt Windows 文件引擎](https://github.com/qt/qtbase/blob/v6.11.1/src/corelib/io/qfsfileengine_win.cpp)。独立双击启动没有相同包重定向时，仍走正常用户目录。
