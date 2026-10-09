# ZloWallpaper 后端需求与接口交接

版本：0.2.0 / API v1。日期：2026-10-09。状态：客户端已实现本文消费接口；业务后端尚待开发。

本文与 `openapi.yaml` 是后端交付基线。变更字段、路径或状态码应先更新契约，再修改客户端和测试。`src/store/DemoServer.*` 是本机开发测试夹具，可作为响应形状参考，不能作为生产后端、安全实现或持久化实现。

## 1. 业务范围与验收目标

产品：Windows 桌面壁纸商城，保留本地壁纸、视频/GIF 播放、Dock、透明任务栏、桌面图标开关、全屏暂停和睡眠恢复。

首期业务闭环：匿名浏览 → 注册/登录 → 创建订单 → 免费领取或测试支付 → 获得购买权益 → 获取下载授权 → 校验下载 → 加入本地库 → 应用到桌面。

本期不包含真实支付渠道、创作者分账、购物车、优惠券、会员和强 DRM。真实收银台未实现；客户端不得把 PENDING 当购买成功，生产后端必须禁止测试支付。

| 需求编号 | 优先级 | 需求 | 验收 |
|---|---|---|---|
| AUTH-01 | P0 | 注册、登录、刷新、登出 | 账号唯一；错误密码失败；刷新令牌旋转；注销使相应会话失效 |
| STORE-01 | P0 | 分类、商品分页/搜索、详情 | 只有上架商品参与公开列表；价格由服务端管理 |
| ORDER-01 | P0 | 免费领取、创建订单、查询订单 | 身份来自会话；客户端不提交价格；重复请求不重复创建业务结果 |
| PAY-TEST-01 | P0（开发环境） | 模拟支付 | 事务内标记订单 PAID 并授予权益；生产环境禁用 |
| OWN-01 | P0 | 获取当前用户已购清单 | 只返回本账号；购买与下载状态独立；多次同步不重复入库 |
| FILE-01 | P0 | 购买后授权下载 | 无权益返回 403；下载授权包含真实大小、SHA-256、版本和期限 |
| ADMIN-01 | P0 | 管理员上传、商品创建/编辑/上下架 | 普通用户不能操作；不允许任意服务器文件路径 |
| OPS-01 | P0 | 错误响应、日志、健康检查、种子数据 | 返回 requestId，方便联调；日志不得包含密码/Token/带票据下载 URL |
| PAY-REAL-01 | 后续 | 真实支付 | 服务端验签、金额核对、回调幂等及退款策略；另行契约评审 |

## 2. 客户端已有边界

- UI：商城、详情、分类、搜索、分页、登录/注册、我的已购、订单、下载进度与取消。
- 网络：Qt Network 异步请求，不要求浏览器 CORS。服务端仍应按未来管理网站的来源策略配置 CORS。
- API：`ApiClient`；账号/商品/订单：`StoreService`；文件下载与入库：`DownloadManager`。
- 不登录、不联网，原本的本地壁纸及桌面美化功能仍可使用。
- 已下载的壁纸保留在本机，即使登出也可以在本地库离线播放。切换账号后，“我的已购”和商城下载映射按账号与服务器隔离。本期不尝试阻止用户复制已下载的原文件。
- 客户端持久化本机下载记录，不把本地绝对路径上传服务器。服务器是购买权益的唯一权威来源。
- 新壁纸文件保存在客户端程序旁的 wallpapers/（本机 E:\zlowallpaper\wallpapers），按服务器与账号分目录；此前下载的旧文件不自动搬迁。此本地位置变化不影响 API 契约。
- 新版本商品文件按 `(API 根地址, userId, wallpaperId, resourceVersion)` 区分；同商品更新版本允许重新下载，旧文件保留，不自动覆盖正在播放的资源。

## 3. 统一协议

API 根地址默认 `http://127.0.0.1:8080/api/v1`；生产地址使用 HTTPS。客户端拒绝非本机的明文 HTTP。根路径固定 `/api/v1`，不携带 query/fragment/用户名密码。

JSON UTF-8，所有 ID、资源版本是字符串；金额为整数分（CNY）；所有时间为 UTC ISO-8601，如 `2026-10-09T10:30:00Z`。`sizeBytes` 使用十进制字符串，防止跨语言大整数精度丢失。

成功（HTTP 200/201）：

```json
{"code":"OK","message":"","data":{},"requestId":"req-123"}
```

失败（保持实际 HTTP 错误状态）：

```json
{"code":"NOT_OWNED","message":"尚未拥有该壁纸，请先购买或领取","data":null,"requestId":"req-124"}
```

| HTTP | 典型 code | 行为 |
|---|---|---|
| 400/422 | VALIDATION_ERROR | 参数错误，展示可读信息 |
| 401 | INVALID_CREDENTIALS / SESSION_EXPIRED | 私有接口返回 401 时客户端清除会话，请用户登录 |
| 403 | NOT_OWNED / FORBIDDEN / TEST_PAYMENT_DISABLED | 无权益/无权限/生产禁止测试支付，不应伪装为 401 |
| 404 | PRODUCT_NOT_FOUND / ORDER_NOT_FOUND | 商品或本账号订单不可用 |
| 409 | USERNAME_EXISTS / IDEMPOTENCY_CONFLICT | 唯一约束或幂等键冲突 |
| 429 | RATE_LIMITED | 限流，不自动高频重试 |
| 500/503 | INTERNAL_ERROR / SERVICE_UNAVAILABLE | 返回 requestId，不把堆栈或数据库内容暴露给客户端 |

私有接口使用 `Authorization: Bearer <accessToken>`。客户端账号密码仅在注册/登录请求中发送；本地不保存密码。accessToken 只在内存，refreshToken 使用 Windows 当前用户 DPAPI 加密保存，且绑定 API 根地址；演示模式不持久化凭证。

API JSON 最大 4 MiB；商品列表每页默认 12、上限 50；封面不超过 4 MiB。V1 已购/订单使用完整数组，应设业务上限（建议最多 200 项）并返回清晰错误，不能静默截断；规模扩大时另发分页契约版本。

API 请求禁止重定向。图片与下载授权 URL 也必须是可直接 GET 的最终 HTTPS（或本机 HTTP）URL，不要依赖 302 跳转登录页面。

## 4. 客户端需要实现的后端接口

| 方法 | 路径（相对 API 根地址） | 权限 | 请求 | data |
|---|---|---|---|---|
| POST | `/auth/register` | 匿名 | username, password | `{username}` |
| POST | `/auth/login` | 匿名 | username, password | Session |
| POST | `/auth/refresh` | refreshToken | refreshToken | Session（新令牌） |
| POST | `/auth/logout` | refreshToken | refreshToken | `{}` |
| GET | `/me` | 登录 | 无 | User（客户端可扩展使用） |
| GET | `/categories` | 匿名 | 无 | Category[] |
| GET | `/wallpapers` | 匿名 | page, pageSize, keyword, categoryId | ProductPage |
| GET | `/wallpapers/{id}` | 匿名 | id | Product |
| POST | `/orders` | 登录 | wallpaperId + Idempotency-Key | Order |
| GET | `/me/orders` | 登录 | 无 | Order[] |
| POST | `/orders/{id}/test-pay` | 登录、测试环境 | Idempotency-Key | Order |
| GET | `/me/wallpapers` | 登录 | 无 | Product[]（已拥有，包括停售但仍可下载的商品） |
| POST | `/wallpapers/{id}/download-grants` | 登录且拥有 | 无 body 字段 | DownloadGrant |

详细字段和参数约束见 `openapi.yaml`。接口不能在未实现时一律返回 200 空对象；客户端会据此错误判断业务完成。

### Session 与 User

```json
{
  "accessToken":"opaque-or-jwt-access-token",
  "refreshToken":"opaque-refresh-token",
  "expiresIn":900,
  "user":{"id":"user-1","username":"alice","displayName":"Alice"}
}
```

用户名 3–32 字符，密码 8–128 字符。后端决定并校验允许的用户名字符；错误信息要明确。生产密码采用适合密码存储的算法，不能复用演示夹具的快速 SHA-256。刷新令牌需要可撤销、可轮换及重放处理；登出接口可使用刷新令牌而不依赖未过期的 accessToken。

客户端在 accessToken 到期前刷新；网络失败延迟重试，明确失效则删除凭证。购买/下载出现 401 时客户端要求重新登录，不默默重放付款请求。

### Product 与列表

```json
{
  "id":"wp-1001",
  "title":"雾山 · 清晨",
  "description":"一张适合专注工作时使用的壁纸。",
  "categoryId":"nature","categoryName":"自然",
  "type":"VIDEO","priceCents":1200,"currency":"CNY",
  "coverUrl":"https://storage.example.com/covers/wp-1001.png",
  "width":1920,"height":1080,"fps":30,
  "resourceVersion":"2","sizeBytes":"18422301","creator":"作者名称"
}
```

`type` 为 IMAGE/GIF/VIDEO；图片 fps 为 0。分页 data 是 `{items: Product[], page: 1, pageSize: 12, total: 60}`。`keyword` 匹配标题，`categoryId` 空值代表全部；服务端使用稳定排序，建议按上架时间倒序再按 ID。公开详情不包含原文件 URL。

### Order 与付款

```json
{
  "id":"order-1001","wallpaperId":"wp-1001","title":"雾山 · 清晨",
  "amountCents":1200,"currency":"CNY","status":"PENDING",
  "testPaymentEnabled":true,"createdAt":"2026-10-09T10:30:00Z"
}
```

- 状态：PENDING / PAID / CANCELED / REFUNDED。v1 客户端展示全部状态，仅对 PENDING 且 `testPaymentEnabled=true` 的订单提供测试付款。
- 价格来自商品数据库；订单记录成交价格快照。客户端只提交 wallpaperId，不接受客户端自行宣告 PAID。
- 免费商品创建订单时直接 PAID 并授予权益。
- `/orders` 与 `/test-pay` 强制 Idempotency-Key。数据库记录请求键、账号、请求摘要和结果；重复相同请求返回相同结果，复用键但内容不同返回 409。
- 对同用户同商品已有的有效购买，或尚未结束的订单，返回既有订单，避免用户刷新后再次创建/付款。
- 测试付款在事务中完成订单状态与权益写入。重复通知/请求不重复授权。
- 已购权益默认随商品更新保留。停售阻止新增购买，不默认剥夺既有下载权。
- 退款后撤销云端授权；已复制到本机的文件不具备强 DRM，本期不会远程删除。

### DownloadGrant 与文件服务

```json
{
  "wallpaperId":"wp-1001","resourceVersion":"2",
  "url":"https://storage.example.com/download/ticket-xxx",
  "expiresAt":"2026-10-09T10:35:00Z",
  "fileExtension":"mp4","sizeBytes":"18422301",
  "sha256":"64个十六进制字符"
}
```

服务器在每次发票据时校验当前会话及权益，票据绑定资源版本与有效期。客户端在文件请求中不转发账号 Authorization，票据 URL 本身授权；存储/CDN 返回 HTTP 200 原始文件，不包 JSON、不返回网页。

文件支持 jpg/jpeg/png/bmp/webp/gif/mp4/webm/mov/avi；原文件最大 10 GiB。服务器保证扩展名和真实格式相符，size/hash 对应实际响应字节，不能在下载时变换编码却沿用旧 hash。票据过期后重新申请；首期不要求断点续传，取消/失败后从头下载。

客户端使用 AtomicFile（解析实际目录后的 QSaveFile）临时写入、流式计算 hash、校验字节总数和 SHA-256 后原子提交，后台生成缩略图，再保存 library.json。持久化失败时不得提示已入库。最多同时处理 3 个下载。旧账号任务在切换账号后取消，不会写到新账号条目。

## 5. 管理端需求（桌面客户端不消费）

后端同样需要这些接口，首期可用 Swagger/Postman 代替管理网站：

| 方法 | 路径 | 要求 |
|---|---|---|
| POST | `/admin/uploads` | multipart file + purpose ORIGINAL/COVER；检测真实媒体格式、大小和 SHA；返回 assetId 与媒体元数据 |
| GET | `/admin/wallpapers` | 分页查看包含未上架的商品 |
| POST | `/admin/wallpapers` | 创建：title、description、categoryId、priceCents、originalAssetId、coverAssetId、creator；初始 DRAFT |
| PATCH | `/admin/wallpapers/{id}` | 修改资料；更换资源由服务器生成新 resourceVersion |
| PUT | `/admin/wallpapers/{id}/publication` | `{status: PUBLISHED 或 WITHDRAWN}`；完整资源验证通过才能上架 |

管理员身份来自服务端角色，不能相信客户端传入 isAdmin。上传文件存储目录由服务器控制，数据库保存对象键/assetId，而非客户端提供的路径。缩略图/视频检查在后台任务中完成，不在事务内长时间编码。

## 6. 建议最小数据模型

- users：id、username 唯一、password_hash、display_name、role、status、时间。
- sessions：用户、refresh_token_hash、有效期、撤销时间、旋转链；不得明文存 refreshToken。
- categories：id、name、排序、状态。
- assets：id、object_key、purpose、mime、extension、size、sha256、width、height、fps、处理状态。
- wallpapers：id、商品字段、category_id、price_cents、currency、当前资源版本、封面资产、发布状态。
- wallpaper_resources：wallpaper_id + version 唯一、original_asset_id；旧版本保留策略明确。
- orders：id、user_id、wallpaper_id、金额快照、状态、支付渠道、created_at。
- entitlements：user_id + wallpaper_id 唯一、来源订单、有效/撤销状态、时间。
- idempotency_records：user_id + operation + key 唯一、请求 hash、响应快照、过期时间。

数据库事务覆盖“支付完成 + 权益创建”。不要把媒体文件直接存大 BLOB，也不需要首期引入微服务。

## 7. 按企业开发流程推进

1. 需求评审：确认本文件的范围、账号规则、免费领取、测试支付、停售/退款及离线策略。
2. 契约评审：以 OpenAPI 为接口基线，冻结 v1 字段；为每个需求编号写成功与失败用例。
3. 后端设计：数据库迁移、权限模型、文件存储、事务/幂等与错误响应。
4. 分阶段实现：商品公开接口 → 鉴权 → 订单/权益 → 文件授权 → 管理接口。
5. 联调：启动本机后端，桌面“连接设置”填写 API 根地址；关闭演示模式，用真实后端账号走闭环。
6. 集成验收：执行下表，再补服务端越权与事务测试。服务端日志和 requestId 可关联。
7. 发布：生产必须 HTTPS，禁止 test-pay；部署健康检查、迁移和备份，并提供版本与回滚说明。客户端当前没有真实收银台，不能因此宣称支持生产收款。

| 验收场景 | 预期 |
|---|---|
| 后端关闭/超时 | 显示连接错误，本地壁纸和 Dock 不受影响 |
| 匿名浏览/搜索/分页 | 正常展示，过滤结果总数准确 |
| 错误密码/重复注册 | 返回明确失败，不产生有效会话 |
| 免费领取/测试购买 | 服务器订单 PAID，权益清单出现且不重复 |
| 未购用户伪造商品 ID 申请下载 | 403，无原文件 URL |
| 用户 B 查询/付款用户 A 的订单 | 404 或 403，不泄漏 A 数据 |
| Token 过期/注销/刷新重放 | 会话规则正确，客户端重新登录 |
| 下载中断/取消/大小不符/hash 不符 | 不创建有效库条目，允许重试 |
| 重复下载/重复同步 | 同账号同版本只入库一次 |
| 下载时换账号/退出 | 旧任务取消，新账号不会继承购买权限 |
| 两台设备同账号 | 已购一致，本机下载状态独立 |
| 商品更新/停售 | 版本可追溯；已购处理符合已确认策略 |
| 非管理员上传/上架 | 服务端拒绝 |
| 生产请求测试付款 | 明确拒绝，不能仅隐藏客户端按钮 |

## 8. 给后端开发对话的开工提示

请读取本仓库 frontend/docs/BACKEND-HANDOFF.md 和 openapi.yaml，再实现独立后端。客户端代码在 frontend/，不要替换 Qt 界面或改变已冻结接口。先给出数据模型/迁移与分阶段计划，再实现商品、鉴权、订单权益、下载授权及管理接口；第一期使用开发环境模拟支付，不接真实收费。演示服务器只供响应形状和测试参考，不可复制其密码哈希、安全或内存存储方案作为生产实现。所有关键验收要有可运行测试，并提供本机启动方式和种子数据。后端源码放 repository/backend/。
