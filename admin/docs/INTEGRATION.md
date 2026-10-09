# 后端联调与验收

## 接口消费清单

根地址配置默认 `http://127.0.0.1:8080/api/v1`。所有 JSON 成功响应为 `{code:"OK",message:"",data:...,requestId:"..."}`；错误保留实际 HTTP 状态且 `data:null`。

| 功能      | 方法 / 相对路径                        | 成功 HTTP | 请求                                                                                    | data                         |
| --------- | -------------------------------------- | --------- | --------------------------------------------------------------------------------------- | ---------------------------- |
| 登录      | POST /auth/login                       | 200       | username、password                                                                      | Session                      |
| 刷新      | POST /auth/refresh                     | 200       | refreshToken                                                                            | 新 Session                   |
| 登出      | POST /auth/logout                      | 200       | refreshToken                                                                            | {}                           |
| 分类      | GET /categories                        | 200       | 无                                                                                      | Category[]                   |
| 验权/目录 | GET /admin/wallpapers                  | 200       | page、pageSize                                                                          | items、page、pageSize、total |
| 素材上传  | POST /admin/uploads                    | **201**   | FormData：file、purpose                                                                 | Asset；编号字段 **id**       |
| 创建草稿  | POST /admin/wallpapers                 | **201**   | title、categoryId、priceCents、originalAssetId、coverAssetId；可选 description、creator | AdminProduct，DRAFT          |
| 编辑      | PATCH /admin/wallpapers/{id}           | 200       | 仅变化的 ProductMutation 字段                                                           | AdminProduct                 |
| 上下架    | PUT /admin/wallpapers/{id}/publication | 200       | status=PUBLISHED 或 WITHDRAWN                                                           | AdminProduct                 |

所有 `/admin/` 请求附带 `Authorization: Bearer <accessToken>`；登录/刷新/登出按原契约，不要求这个头。上传不手动写 Content-Type，由浏览器生成 multipart boundary。没有 GET admin 详情、角色字段、资产轮询或删除接口。

登录流程：POST 登录 → GET 管理目录验权 → GET 分类 → 展示目录。不是根据用户名 `admin` 或 JWT payload 判断权限。只有模拟夹具预置模拟账户。

创建流程：分别上传 COVER / ORIGINAL → 保留返回 data.id → 两份资产 READY → POST 创建 → 初始 DRAFT → 管理员确认 PUT 上架。创建不发送宽高、帧率、版本、币种和发布状态。

编辑流程：使用列表的完整 AdminProduct 填充；默认保留两份已有资产；只提交变化字段；换原文件由服务端创建新版本；资料保存不隐式改上下架状态。

## CORS 配置

示例来源：`http://127.0.0.1:4173`（若改端口或使用 localhost，也要分别配置）。

```text
Access-Control-Allow-Origin: http://127.0.0.1:4173
Vary: Origin
Access-Control-Allow-Methods: GET, POST, PATCH, PUT, OPTIONS
Access-Control-Allow-Headers: Authorization, Content-Type
```

允许未认证 OPTIONS 预检，不要先要求 OPTIONS 携带 Bearer；真正业务请求仍需鉴权。实际成功与失败响应都带允许来源头。前端不携带 cookie，不发送凭证到封面 URL。

## 会话与故障规则

- accessToken / refreshToken / 密码不写 localStorage、sessionStorage、cookie、URL 或日志。Web 管理端不能使用桌面 DPAPI，因此两种 Token 都只放内存。
- 到期前 45 秒窗口内刷新，30 秒定时检查；私有请求执行前也检查；并发刷新共享一个请求。刷新旋转令牌；401 删除本机会话，不自动重放创建/上传/上下架。
- 私有接口 401：关闭编辑器、清理页面和旧请求，要求重新登录。403：显示权限不足，不伪装成密码错误或会话过期。
- 400/422、404、409、429、500/503：显示后端 message、code、HTTP 状态和 requestId；不自动高频重试。
- API 网络、CORS、超时：可读错误；真实模式绝不自动切回模拟数据。普通 JSON 超时 30 秒，上传 120 秒。
- 超时后的写操作可能已经在服务端完成；先刷新目录/查日志确认，再由管理员决定重试。现有管理员契约没有幂等键，不能安全自动重放。
- 关闭或刷新页面后必须重新登录；后端会话撤销与过期仍是服务器的职责。

## 自动化检查

`npm test`：Node 内置运行器，19 项测试。涵盖金额/URL/文件边界、请求字段、登录 401、普通账号 403、分页、上传、创建/编辑/发布、版本更换、PROCESSING/FAILED、刷新旋转和重放、注销、空列表、503、401、真实本机 HTTP Bearer/multipart/CORS、响应协议与大小、超时/取消、禁止写请求重放、静态服务路径边界，并验证写操作返回的商品/状态与请求一致、并发私有请求只刷新一次。

真实 HTTP 检查使用 `tests/http-fixture.mjs` 创建临时本机服务并在测试结束关闭。它是测试夹具，使用内存会话和模拟元数据，**不是临时业务后端，不应部署或接真实用户**。

## 浏览器重跑

启动页面服务后安装可选开发依赖：`npm ci`。命令行验收示例：

```powershell
npx playwright-cli -s=zlo-admin open http://127.0.0.1:4173 --browser msedge
npx playwright-cli -s=zlo-admin snapshot
# 按 snapshot 元素引用填写登录；操作后重新获取 snapshot。
npx playwright-cli -s=zlo-admin resize 1440 1000
npx playwright-cli -s=zlo-admin screenshot --filename output/playwright/catalog-desktop.png
npx playwright-cli -s=zlo-admin resize 390 844
npx playwright-cli -s=zlo-admin screenshot --filename output/playwright/catalog-mobile.png
```

仅为验证真实接口模式和浏览器跨域传输，可在**独立测试终端**临时运行：

```powershell
node tests/http-fixture.mjs
```

默认测试端口 8080，可通过 `$env:FIXTURE_PORT = '8081'` 修改。它可能与实际后端端口冲突，不要在后端已运行时启动。页面切换真实模式并填对应端口，登录模拟 admin / Admin12345，可验证真实 fetch、CORS、multipart。验收后 Ctrl+C 关闭夹具，接入真实后端时必须用后端账号。

## 验收清单

| 场景                 | 预期                                                     |
| -------------------- | -------------------------------------------------------- |
| 首次打开             | 默认模拟模式；明确模拟账号与内存数据提示                 |
| 密码错误             | 401 INVALID_CREDENTIALS；仍在登录页                      |
| viewer 登录          | 管理接口 403；不能进入工作台                             |
| 管理员登录           | 14 条种子商品；12 条第一页、2 条第二页                   |
| 分别上传封面、原文件 | 返回 id；READY 显示资产编号、真实文件大小和 SHA 对应字节 |
| 创建/编辑            | DRAFT；价格 12.99 发送 1299；PATCH 只含修改字段          |
| 确认上架/下架        | 对应 PUT；取消不请求；成功刷新目录                       |
| PROCESSING/FAILED    | 不允许保存、不擅自轮询或自动变 READY                     |
| 空列表               | 引导创建；无假成功表格                                   |
| 服务 503             | 显示错误与 requestId；切回正常可重新加载                 |
| 权限被撤销 403       | 清空表格展示权限不足；不等同 401                         |
| 私有请求 401         | 清理会话并退回登录；旧任务不回写                         |
| 切换 API 或模式      | 明确退出，取消旧请求；不得串数据或令牌                   |
| 真实 API 未启动      | 连接错误；不会出现模拟商品                               |
| 手机 390 × 844       | 页面不横向溢出；表格区域可横向滚动；编辑面板可完整滚动   |
| 浏览器存储           | 仅模式/API 地址；无密码、Token                           |

以上夹具验收不能替代后端集成验收。真实后端上线前还需验证跨账号资产引用、权限撤销、真实格式/大小检查、非法发布、事务边界、上传孤立资产清理、限流与生产 HTTPS。
