# BlogServer API 接口文档

> 生成日期：2026-07-24 | 基于 `D:\workspace\BlogServer\Server\src\Controller\` 源码分析

## 通用约定

### 响应格式

所有 JSON API 响应遵循统一格式：

| 字段 | 类型 | 说明 |
|------|------|------|
| `result` | int | `200` = 成功，`500` = 错误，`400` = 未找到 |
| `msg` | string | 错误时的人类可读描述（成功时省略） |

### 认证机制

- 基于 Cookie 的 Session 认证
- 登录成功后，服务器返回 `sessionId` cookie（HttpOnly, SameSite=Strict, 生产环境 Secure）
- Session 存储在服务端内存中，默认 1 小时过期（`SessionHoldTime` 配置项）
- **5 次登录失败后账号锁定 60 分钟**
- 前端所有 `fetch` 调用需带 `credentials: 'include'`

### 静态文件服务

默认情况下，`/assets/{filename}` 和 `/file/{filename}` 会映射到 `web/assets/` 目录下的文件，支持 HTTP Range 请求（206 部分内容）。

---

## API 端点详表

### 一、前台公开接口

#### 1. 首页/文章列表（分页）

```
GET /
GET /page={page}
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 路径参数 | `page` — 页码（数字，默认 1） |
| 响应 | HTML 页面（`index.html` 模板渲染） |
| 模板变量 | `posts` 数组（每页 10 篇）, `pageForward`, `pageNext`, `currentPage`, `pageNums`, `blogViewTimes`, `blogTotalNum` |
| 每篇包含 | `id`, `title`, `profile`, `blogtitlepic`, `categoryName`, `created_at` |

---

#### 2. 文章详情页

```
GET /post/{id}
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 路径参数 | `id` — 文章 ID（数字） |
| 响应 | HTML 页面（`article.html` 模板渲染） |
| 模板变量 | `post` 对象（`titlepic`, `title`, `created_at`, `updated_at`, `categoryName`, `content`, `viewTimes`） |
| 副作用 | 每次访问该文章的 `view_times` +1 |
| 异常 | 文章不存在 → 302 到 `/404`；分类查找失败 → 302 到 `/505` |

---

#### 3. 获取单篇文章（JSON API）

```
GET /api/blog/{id}
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 路径参数 | `id` — 文章 ID（数字，必填） |
| 响应 Content-Type | `application/json` |
| 成功响应 | `{ "result": 200, "id": <uint64>, "title": "...", "category_id": <uint64>, "profile": "...", "titlepic": "...", "content": "...", "tags": "...", "status": <int>, "created_at": <uint64>, "updated_at": <uint64>, "view_times": <uint64> }` |
| 错误 | `{"result":500,"msg":"Id Error"}` / `{"result":500,"msg":"Post not exist"}` |
| 注意 | 返回的 `content` 已经过 HTML 反转义还原为原始 Markdown |

---

#### 4. 按分类查看文章列表

```
GET /category/id={id}&page={page}
GET /category/id={id}
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 路径参数 | `id` — 分类 ID（数字，必填）；`page` — 页码（数字，默认 1） |
| 响应 | HTML 页面（`category-detail.html` 模板渲染） |
| 模板变量 | `posts` 数组（每页 10 篇），分页信息，`category` 对象 |
| 异常 | 无效分类 ID → 302 到 `/404` |

---

#### 5. 所有分类列表页

```
GET /categories
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 响应 | HTML 页面（`category.html` 模板渲染） |
| 模板变量 | `categories` 数组（完整 Category 对象） |
| 异常 | 分类列表为空 → 302 到 `/404` |

---

#### 6. 获取单个分类（JSON API）

```
GET /api/category/{id}
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 路径参数 | `id` — 分类 ID（数字，必填） |
| 成功响应 | `{ "result": 200, "id": <uint64>, "name": "...", "categorypic": "..." }` |
| 错误 | `{"result":500,"msg":"Id Error"}` / `{"result":500,"msg":"Post not exist"}` |

---

#### 7. 获取所有分类（JSON API）

```
GET /api/categories
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 成功响应 | `{ "result": 200, "categories": [ {"id": <uint64>, "name": "..."}, ... ] }` |

---

#### 8. "关于"页面

```
GET /about
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 行为 | 302 重定向到标题为 "关于网站" 的文章详情页 |
| 异常 | 找不到文章 → 302 到 `/404` |

---

### 二、后台管理接口（需登录）

#### 9. 登录

```
POST /api/login
```

| 项目 | 说明 |
|------|------|
| 认证 | 无（用于登录） |
| Content-Type | `application/json` |
| 请求体 | `{ "username": "<string>", "userpassword": "<string>" }` |
| 成功 | `{"result":200}` + `Set-Cookie: sessionId=...` 响应头 |
| 失败 | `{"result":500,"msg":"Wrong username or psw"}` |
| 安全 | 5 次失败后锁定 60 分钟；凭据来自 `server.cfg` 中的 `AdminUser`/`AdminPassword` |

---

#### 10. 创建/修改文章

```
POST /api/blog
```

| 项目 | 说明 |
|------|------|
| 认证 | **必须**（检查 `sessionId` cookie） |
| Content-Type | `application/json` |
| 请求体（新增） | `{ "category_id": "<数字字符串>", "title": "...", "profile": "...", "content": "...", "titlepic": "..." }` |
| 请求体（修改） | `{ "id": "<数字字符串>", "category_id": "...", "title": "...", "profile": "...", "content": "...", "titlepic": "..." }` |
| 判断逻辑 | 请求体含 `"id"` 字段 → 修改操作；否则 → 新增操作 |
| 成功 | `{"result":200}` |
| 失败 | `{"result":500,"msg":"<具体错误>"}` ；未登录：`{"result":500,"msg":"Not login"}` |
| 注意 | 所有字段均必填不能为空；内容会自动 HTML 转义后存储 |

---

#### 11. 删除文章

```
DELETE /api/blog/{id}
```

| 项目 | 说明 |
|------|------|
| 认证 | **必须** |
| 路径参数 | `id` — 文章 ID（数字字符串，必填） |
| 成功 | `{"result":200}` |
| 失败 | `{"result":500,"msg":"Not login"}` / `{"result":500,"msg":"No id error!"}` |

---

#### 12. 创建/修改分类

```
POST /api/category
```

| 项目 | 说明 |
|------|------|
| 认证 | **必须** |
| Content-Type | `application/json` |
| 请求体（新增） | `{ "name": "...", "categorypic": "..." }` |
| 请求体（修改） | `{ "id": "<数字字符串>", "name": "...", "categorypic": "..." }` |
| 成功 | `{"result":200}` |
| 失败 | `{"result":500,"msg":"Not login"}` |
| 注意 | `name` 为必填；`categorypic` 为空时回退到配置的 `defaultPic` |

---

#### 13. 删除分类

```
DELETE /api/category/{id}
```

| 项目 | 说明 |
|------|------|
| 认证 | **注意：源码中未检查登录状态**（疑似 bug） |
| 路径参数 | `id` — 分类 ID（数字字符串，必填） |
| 成功 | `{"result":200}` |
| 失败 | `{"result":500,"msg":"No id error!"}` / `{"result":500,"msg":"Id Error!"}` |

---

#### 14. 后台登录页面

```
GET /background
```

| 项目 | 说明 |
|------|------|
| 认证 | 无；但已登录用户会被 302 重定向到 `/background/articles` |
| 响应 | HTML（`backyard-login.html`） |

---

#### 15. 文章管理页面

```
GET /background/articles
GET /background/articles?id-begin={lastId}
```

| 项目 | 说明 |
|------|------|
| 认证 | **必须**；未登录 → 302 到 `/background` |
| 查询参数 | `id-begin` — 游标分页的起始 ID（数字，默认 0） |
| 响应 | HTML（`backyard-articles.html` 模板渲染） |
| 模板变量 | `posts` 数组（每页 25 篇，按 ID 降序），每篇含 `id`, `title`, `created_at`, `updated_at` |

---

#### 16. 分类管理页面

```
GET /background/categories
```

| 项目 | 说明 |
|------|------|
| 认证 | **必须**；未登录 → 302 到 `/background` |
| 响应 | HTML（`backyard-categories.html` 模板渲染） |
| 模板变量 | `categories` 数组（所有分类），每项含 `id`, `name` |
| 注意 | `id-begin` 参数虽被解析但实际未使用，始终返回全部分类 |

---

### 三、系统/运维接口

#### 17. 404 页面

```
GET /404
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 响应 | HTML（`error/404/baisc404.html`） |

---

#### 18. 服务器优雅关闭

```
GET /shutdown
```

| 项目 | 说明 |
|------|------|
| 认证 | **IP 限制**：仅当请求来源 IP 严格等于 `127.0.0.1` 时才执行关机 |
| 响应 | 始终 302 重定向到 `/404`（无论是否执行关闭） |
| 关闭流程 | 设置关闭标志 → 退出事件循环 → 停止接收新连接 → 依次析构各组件的单例 |

---

#### 19. Certbot SSL 证书验证

```
GET /.well-known/acme-challenge/{filename}
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 路径参数 | `filename` — ACME 挑战文件令牌 |
| 响应 | 来自 `temp/certbot/.well-known/acme-challenge/` 目录的原始文件内容 |

---

### 四、测试接口

#### 20. 测试留言板页面

```
GET /test/message
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 响应 | HTML（`testMessage.html`） |

---

#### 21. 发送测试消息

```
POST /test/message
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| Content-Type | `application/json` |
| 请求体 | `{ "name": "<string>", "content": "<string>" }` |
| 限制 | 请求体最大 2048 字节 |
| 成功 | `{"result":200}` |
| 失败 | `{"result":500,"msg":"Request body too long!"}` / `{"result":500,"msg":"Json Error!"}` |

---

#### 22. 按页获取测试消息

```
GET /test/message/page={page}
```

| 项目 | 说明 |
|------|------|
| 认证 | 无 |
| 路径参数 | `page` — 页码（数字，默认 0） |
| 成功 | `{ "messages": [{"name":"...","content":"..."}, ...], "pagenum": <int>, "result": 200 }` |
| 分页 | 每页 10 条 |

---

## 端点快速索引

| # | 方法 | 路径 | 认证 | 说明 |
|---|------|------|------|------|
| 1 | GET | `/` / `/page={page}` | 无 | 首页（HTML） |
| 2 | GET | `/post/{id}` | 无 | 文章详情页（HTML） |
| 3 | GET | `/api/blog/{id}` | 无 | 文章 JSON |
| 4 | GET | `/category/id={id}&page={page}` | 无 | 分类文章列表（HTML） |
| 5 | GET | `/categories` | 无 | 全部分类页（HTML） |
| 6 | GET | `/api/category/{id}` | 无 | 单个分类 JSON |
| 7 | GET | `/api/categories` | 无 | 全部分类 JSON |
| 8 | GET | `/about` | 无 | 关于页面（302） |
| 9 | POST | `/api/login` | 无 | 登录 |
| 10 | POST | `/api/blog` | **必须** | 创建/修改文章 |
| 11 | DELETE | `/api/blog/{id}` | **必须** | 删除文章 |
| 12 | POST | `/api/category` | **必须** | 创建/修改分类 |
| 13 | DELETE | `/api/category/{id}` | 遗漏 | 删除分类（缺少认证检查） |
| 14 | GET | `/background` | 无* | 管理登录页 |
| 15 | GET | `/background/articles` | **必须** | 文章管理页 |
| 16 | GET | `/background/categories` | **必须** | 分类管理页 |
| 17 | GET | `/404` | 无 | 404 页面 |
| 18 | GET | `/shutdown` | IP | 优雅关机 |
| 19 | GET | `/.well-known/acme-challenge/{f}` | 无 | Certbot 验证 |
| 20 | GET | `/test/message` | 无 | 测试留言板页 |
| 21 | POST | `/test/message` | 无 | 发送测试消息 |
| 22 | GET | `/test/message/page={page}` | 无 | 获取测试消息 JSON |
