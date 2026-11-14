# BlogServer 运行与维护指南

## 项目概述

BlogServer 是 `feiqi3.cn` 博客网站的后端 HTTP 服务器，使用 C++20 手写。它是一个**多线程、单进程**的 HTTP 服务器，支持 HTTP/1.1 和 HTTP/2（TLS），内嵌 SQLite3 数据库，使用 Inja 模板引擎做服务端渲染。

- **仓库路径**：`D:\workspace\BlogServer`
- **前端项目**：`D:\workspace\blogweb`（博客前端，Vite 构建后部署到本项目的 `Server/resources/web/` 目录）
- **框架层**：`FeiLib`（作者自研的 C++ HTTP 框架，位于 `FeiLib/` 子目录）
- **编译器**：MinGW-w64（gcc），C++20
- **构建系统**：CMake 3.10+

---

## 1. 目录结构

```
BlogServer/
├── CMakeLists.txt               # 根 CMake（项目管理）
├── FeiLib/                      # 自研 HTTP 框架（含子模块：spdlog, json, inja, re2, absl, oneTBB, nghttp2, wepoll）
├── Server/                      # 博客服务主程序
│   ├── CMakeLists.txt           # 服务 CMake（含 SERVER_RESOURCE_DIR 编译宏）
│   ├── src/
│   │   ├── Server/              # main() 入口 + 服务器生命周期管理
│   │   ├── Controller/          # HTTP 路由处理器（MVC 风格）
│   │   ├── Core/                # 配置加载、会话管理、模板渲染、基础定义
│   │   ├── DAO/                 # 数据访问层（ORM + SQLite3 包装）
│   │   ├── Model/               # 数据模型（带编译时反射注解）
│   │   ├── Service/             # 业务逻辑层
│   │   └── Utils/               # 工具类（文件缓存、HTML 转义、分页等）
│   ├── resources/               # 运行时资源（**编译时路径固定**）
│   │   ├── config/server.cfg    # 主配置文件
│   │   ├── config/h2PushPromise.cfg  # HTTP/2 推送资源清单
│   │   ├── database/blog.db     # SQLite3 数据库文件
│   │   ├── SSL/                 # TLS 证书和私钥
│   │   └── web/                 # 前端静态资源
│   │       ├── page/            # HTML 页面模板（.html，含 Inja 模板语法）
│   │       ├── assets/          # 编译后的 JS/CSS/图片
│   │       └── img/             # 图片资源
│   └── Tools/                   # 辅助脚本（SSL 续期、字体烘焙、关机脚本）
├── Client/                      # TCP 回显测试客户端
├── Tests/                       # 单元/集成测试
└── build_mingw/                 # CMake 构建输出目录（MinGW Release）
    ├── release/                 # 静态库 和 DLL 文件
    └── Bin/Release/             # FeiLib.dll 等
```

---

## 2. 构建

### 2.1 前提条件

| 依赖 | 来源 | 说明 |
|------|------|------|
| MinGW-w64 (gcc) | 系统安装（`D:\Program\mingw64\`） | 支持 C++20 |
| CMake >= 3.10 | 系统安装（`C:\Program Files\CMake\`） | 构建生成器 |
| OpenSSL | 系统安装 | TLS 支持 |
| Git 子模块 | `git submodule update --init --recursive` | 第三方库源码 |

### 2.2 构建命令

```bash
# 1. 初始化子模块（首次或子模块有更新时）
cd D:/workspace/BlogServer
git submodule update --init --recursive

# 2. 配置 CMake（使用 MinGW Makefiles 生成器）
cd D:/workspace/BlogServer/build_mingw
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build . -j$(nproc)
# 或者:
make -j$(nproc)
```

### 2.3 构建产物

构建完成后产物位置：
- **Server 可执行文件**：`build_mingw/release/Server.exe`
- **FeiLib DLL**：`build_mingw/Bin/Release/libFeiLib.dll`（会被复制到 target 目录）
- **其他 DLL**：`build_mingw/release/libnghttp2.dll`, `libsqlite3.dll`, `libtbb12.dll`

### 2.4 构建注意事项

- CMake 编译宏 `SERVER_RESOURCE_DIR` 被硬编码为 `Server/resources/` 的**构建时绝对路径**，所以如果不重新配置 CMake，不要移动 `resources/` 目录。
- 根 CMakeLists.txt 约定了 `Debug` → `build_mingw/debug/`，`Release` → `build_mingw/release/` 的输出路径。
- `BlogGenTools` 选项（默认 ON）会额外构建 `Tools/` 下的辅助工具。

---

## 3. 配置文件

### 3.1 主配置：`Server/resources/config/server.cfg`

格式为自定义 INI/TOML 风格：

```ini
env: prod

[[prod]]
AdminUser: feiqi3
AdminPassword: zn.116945
TcpIdleTime: 5000
SocketKeepAliveTime: 5
HttpRequestWaitTime: 15
HttpConnectTimeout: 60
ALPNPreference: http2
H2MaxStreamNum: 10
H2EnablePush: 1
AboutPageTitle: 关于网站
LinksPageTitle: 友链
```

**可选的配置项（均可省略，有默认值）：**

| 键 | 默认值 | 说明 |
|---|---|---|
| `AdminUser` | `"admin"` | 后台管理用户名 |
| `AdminPassword` | `"admin"` | 后台管理密码 |
| `TcpIdleTime` | - | TCP 空闲超时 |
| `SocketKeepAliveTime` | - | Socket keep-alive 时间 |
| `HttpRequestWaitTime` | - | HTTP 请求超时 |
| `HttpConnectTimeout` | - | HTTP 连接超时 |
| `ALPNPreference` | - | TLS ALPN 协商（`http2` 开启 HTTP/2） |
| `H2MaxStreamNum` | `10` | 每连接最大 HTTP/2 并发流 |
| `H2EnablePush` | `1` | 启用 HTTP/2 服务器推送 |
| `Database` | `resources/database/blog.db` | 数据库文件路径 |
| `SessionHoldTime` | `3600000` | 会话过期时间（毫秒，默认 1 小时） |
| `TemplateCacheTime` | `86400000` | 模板缓存过期时间（毫秒，默认 24 小时） |
| `FileCacheHoldTime` | `3600000` | 文件缓存过期时间（毫秒，默认 1 小时） |
| `defaultPic` | - | 创建分类时的默认图片路径 |
| `AboutPageTitle` | `"关于网站"` | `/about` 路由按此标题查 Posts 并渲染文章内容（保持在 `/about`，不跳转）（FrontGroundController::About） |
| `LinksPageTitle` | `"友链"` | `/links` 路由按此标题查 Posts 并渲染文章内容（保持在 `/links`，不跳转）（FrontGroundController::Links，2026-08-22 新增） |
| `InfoPageTitle` | `"信息"` | `/info` 路由按此标题查 Posts 并渲染文章内容（保持在 `/info`，不跳转）（FrontGroundController::Info，2026-08-25 新增） |

> 业务层配置项的分模块完整清单另见 `Server/doc/CONFIGURATIONS.md`（FeiLib 层配置见 `FeiLib/README.md`）。

### 3.2 HTTP/2 推送配置：`Server/resources/config/h2PushPromise.cfg`

定义访问某个路径时自动推送的静态资源列表（格式为路径 → 资源列表）：

```
/ {
    /assets/marked.min-Dj7pE-zL.js,
    /assets/article-BKHrn4MY.css,
    /assets/article-CWMhKrli.js
}
```

**注意**：此文件中的资源路径包含 Vite 构建的 hash，更新前端后需同步更新此文件。

### 3.3 数据库 DDL：`Server/resources/database/DbCreateSql.txt`

定义了 4 张表：`Posts`、`Categories`、`BlogStatus`、`TestMessage`。

### 3.4 数据库文件：`Server/resources/database/blog.db`

实际的 SQLite3 数据库文件。支持 WAL 日志模式，外键约束已启用。每个线程使用独立的数据库连接（线程本地）。

---

## 4. 运行服务器

### 4.1 运行前检查清单

1. **构建已完成**：确认 `build_mingw/release/Server.exe` 存在
2. **DLL 在同一目录**：确保 `libFeiLib.dll`, `libnghttp2.dll`, `libsqlite3.dll`, `libtbb12.dll` 与 `Server.exe` 在同一目录
3. **配置文件存在**：`Server/resources/config/server.cfg`
4. **TLS 证书存在**：`Server/resources/SSL/cert.pem` 和 `Server/resources/SSL/private.pem`
5. **数据库文件存在**：`Server/resources/database/blog.db`
6. **端口可用**：端口 80 和 443 未被占用

### 4.2 启动命令

```bash
cd D:/workspace/BlogServer/build_mingw/release
./Server.exe
```

服务器启动后会：
- 监听 **端口 80**（HTTP）
- 监听 **端口 443**（HTTPS/TLS）
- 每 20 秒执行一次后台事件：文件缓存过期检查、会话过期检查、BlogData 同步

### 4.3 运行日志

服务器通过 FeiLib 的 `Fei::Logger`（基于 spdlog）输出日志到控制台。启动和关闭时分别输出 `"Server Shutdown."` 等关键日志。

---

## 5. 正常关闭服务器

### 方法 A：优雅关闭（推荐）

向 `http://localhost/shutdown` 发送 HTTP GET 请求，**只有来自 127.0.0.1 的请求才会触发关闭**（防止远程恶意关机）：

```bash
curl http://localhost/shutdown
```

或使用 Python 脚本：

```bash
cd D:/workspace/BlogServer/Server/Tools/scripts
python server_shutdown.py
```

**优雅关闭的执行顺序**：
1. 设置 `shouldClose = true`
2. 主事件循环退出 sleep 等待
3. 调用 `server->stop()` 停止接受新连接
4. 依次析构：`FHttpServer` → `TemplateRender` → `DatabaseOperation` → `AdminLogin` → `SessionManager` → `BlogData`
5. 反初始化 SSL 环境
6. 反初始化 FeiLib
7. 日志输出 `"Server Shutdown."`

### 方法 B：Ctrl+C（硬关闭）

在运行服务器的终端中按 Ctrl+C 直接终止进程。不推荐，可能导致数据库写入丢失。

---

## 6. API 端点速查

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/` | 博客首页（分页，每页 10 篇，`?page=n`） |
| GET | `/404` | 自定义 404 页面 |
| GET | `/shutdown` | 优雅关机（仅限 localhost） |
| GET | `/assets/{name}` | 静态资源文件 |
| GET | `/api/blog/{id}` | 根据 ID 获取博客文章 JSON |
| GET | `/api/category/{id}` | 获取分类 JSON |
| GET | `/api/categories` | 获取所有分类 JSON |
| POST | `/api/login` | 管理登录（JSON body: `{username, userpassword}`） |
| POST | `/api/blog` | 创建/更新文章（需认证） |
| DELETE | `/api/blog/{id}` | 删除文章（需认证） |
| POST | `/api/category` | 创建/更新分类（需认证） |
| DELETE | `/api/category/{id}` | 删除分类（需认证） |
| GET | `/background` | 管理后台登录页面 |
| GET | `/background/articles` | 文章管理页面（需登录） |
| GET | `/background/categories` | 分类管理页面（需登录） |
| GET | `/.well-known/acme-challenge/{file}` | ACME 证书验证（Certbot） |
| GET | `/test/message` | 测试留言板页面 |

---

## 7. 认证机制

- Cookie-based：登录成功后设置 `sessionId` cookie（HttpOnly, SameSite=Strict, Secure）
- 服务端内存会话存储（并发哈希表），默认 1 小时过期
- 连续 5 次登录失败后账号锁定 60 分钟
- 前端所有 `fetch` 调用需带 `credentials: 'include'`
- API 响应格式：`{ result: 200, ... }` — 检查 `result === 200` 表示成功

---

## 8. 部署流程（从 blogweb 前端到 BlogServer）

1. 在 `D:\workspace\blogweb` 中运行 `npm run build`
2. 将 `dist/*.html` → `Server/resources/web/page/`
3. 将 `dist/assets/*` → `Server/resources/web/assets/`
4. 将 `dist/*.jpg|ico|svg|png` → `Server/resources/web/page/`
5. 重启 BlogServer（先 curl `localhost/shutdown`，再启动 `Server.exe`）

---

## 9. 常见问题排查

### 端口被占用
检查端口 80/443：
```bash
netstat -ano | grep ":80 "
netstat -ano | grep ":443 "
```

### 数据库锁定
服务器非正常关闭可能导致 WAL 文件残留。正常启动时会自动恢复。如果需要重置：
```bash
rm D:/workspace/BlogServer/Server/resources/database/blog.db-shm
rm D:/workspace/BlogServer/Server/resources/database/blog.db-wal
```

### DNS/域名
服务器不依赖域名配置，它监听所有本地接口。生产部署时由外部 DNS 将 `feiqi3.cn` 指向服务器 IP。

### TLS 证书过期
使用 Server/Tools/Certbot_GenSSL/genSSL.sh 可以自动续期 Let's Encrypt 证书并重启服务器。
