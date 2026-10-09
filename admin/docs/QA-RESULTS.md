# 本次验收记录

日期：2026-10-09。页面使用本地静态 HTTP 服务，浏览器为 Edge，通过 Playwright CLI 操作。

## 自动化

`npm test`：**19 / 19 通过**。无跳过。测试运行时创建临时 HTTP 服务并在结束关闭，不依赖实际业务后端。

## 浏览器操作记录

- 错误密码停留在登录页，展示 HTTP 401 / INVALID_CREDENTIALS / requestId。
- viewer 账号登录后管理接口返回 HTTP 403 / FORBIDDEN，不能进入工作台。
- admin 账号登录正常，14 个初始商品，第一页 12 条，第二页 2 条。
- 生成 PNG 测试素材并分别选择封面/原文件，显示 READY、两个不同的 data.id；创建按钮从禁用变可用。
- 价格 12.99 创建后目录显示 ¥12.99，状态为草稿。确认上架后显示已上架。
- 编辑标题时保留两份资产编号与资源版本；确认下架后显示已下架。
- PROCESSING 上传返回明确提示并保持保存禁用；没有未定义的轮询请求。
- 模拟空列表、503、403、正常恢复、401 场景执行完成；401 退回登录并清空密码与工作区。
- 手机 390 × 844：初次发现表格撑大页面，修复后 `document.documentElement.scrollWidth = 390`；编辑器可滚动且内容宽度等于容器宽度 354，不再横向溢出。
- 配置真实模式到未启动端口 18082，显示 NETWORK_ERROR，数据模式仍为“真实接口”，没有展示模拟目录。
- 真实模式连接测试专用夹具端口 18081，跨域 fetch 完成登录、目录、两次 multipart 上传、创建、上架、PATCH 编辑、下架；浏览器请求记录中 POST 上传/创建为 201，其他业务成功响应为 200。
- 浏览器 localStorage 只有 `zlo.admin.preferences`（mode/base）；sessionStorage 没有任何项，凭证没有写入持久化存储。
- 修复了初次浏览器检查时 CSP 中 IPv6 来源字面量的不兼容问题。正常页面加载没有脚本运行错误；故意连接未启动 API 会产生浏览器网络错误，这是预期故障场景。

真实模式验收的服务是 **tests/http-fixture.mjs**，不是业务后端。未验证真正的用户数据库、生产角色系统、后台媒体任务或业务持久化；这些需要后端完成后重新验收。

## 本机截图

截图输出被 Git 忽略，位于 `output/playwright/`：

- login-desktop.png
- catalog-desktop.png
- editor-desktop.png
- catalog-mobile.png
- editor-mobile.png
- catalog-real-http.png

接口基线文件的 SHA-256 在实现前后完全一致，见 CONTRACT-GAPS.md。Git 工作区只新增 admin/，没有修改 frontend/ 或原运行目录。
