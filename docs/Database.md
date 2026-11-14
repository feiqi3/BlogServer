# BlogServer 数据库表文档

> 生成日期：2026-07-24 | 基于 `D:\workspace\BlogServer\Server\` 源码分析

## 概述

- **数据库引擎**：SQLite3（嵌入式，合并版源码 `Server/3rd/sqlite3/`）
- **DDL 文件**：`Server/resources/database/DbCreateSql.txt`
- **数据库文件**：`Server/resources/database/blog.db`
- **日志模式**：WAL（Write-Ahead Logging）
- **外键**：已启用
- **线程安全**：每个线程独立的数据库连接（`thread_local sqlite3*`），预编译语句缓存
- **ORM**：编译时反射驱动，通过 `ORM.h` 中的 `Query<T>` 模板类构建 SQL

---

## ORM 关键宏

| 宏 | 用途 |
|---|---|
| `ENTITY_TABLE(TB_NAME)` | 将 C++ 类映射到数据库表名 |
| `AUTO_INC_PK(KEY_NAME)` | 声明自增主键，INSERT 时跳过该列 |
| `FIELD(CLS, MEMBER)` | 创建类型安全的字段引用 |
| `PARAM` | Where 子句中的参数化占位符（防 SQL 注入） |

---

## 表 1：Posts（文章表）

### DDL

```sql
CREATE TABLE "Posts" (
    "id"         INTEGER NOT NULL UNIQUE,
    "user_id"    INTEGER,
    "title"      TEXT    NOT NULL,
    "category_id" INTEGER NOT NULL,
    "profile"    TEXT    NOT NULL,
    "titlepic"   TEXT,
    "content"    TEXT    NOT NULL,
    "tags"       TEXT,
    "status"     INTEGER NOT NULL,
    "created_at" INTEGER NOT NULL,
    "updated_at" INTEGER,
    "view_times" INTEGER NOT NULL,
    FOREIGN KEY("category_id") REFERENCES "Categories"("id"),
    PRIMARY KEY("id" AUTOINCREMENT)
);
```

### 列说明

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| id | INTEGER | PK, AUTOINCREMENT, NOT NULL | 文章唯一 ID |
| user_id | INTEGER | - | 作者 ID（预留，当前未使用） |
| title | TEXT | NOT NULL | 文章标题 |
| category_id | INTEGER | NOT NULL, FK→Categories(id) | 所属分类 ID |
| profile | TEXT | NOT NULL | 文章摘要/简介 |
| titlepic | TEXT | - | 题图路径 |
| content | TEXT | NOT NULL | 文章正文（HTML 转义后的 Markdown） |
| tags | TEXT | - | 标签（逗号分隔字符串） |
| status | INTEGER | NOT NULL | 文章状态（发布/草稿等） |
| created_at | INTEGER | NOT NULL | 创建时间（Unix 时间戳） |
| updated_at | INTEGER | - | 最后修改时间（Unix 时间戳） |
| view_times | INTEGER | NOT NULL | 浏览次数计数器 |

### C++ 模型

```cpp
namespace Blog::Model {
class Post {
public:
  uint64_t id;
  uint64_t user_id;
  std::string title;
  uint64_t category_id;
  std::string profile;
  std::string titlepic;
  std::string content;
  std::string tags;
  int status;
  uint64_t created_at;
  uint64_t updated_at;
  uint64_t view_times;
  ENTITY_TABLE(Posts);
  AUTO_INC_PK(id);
};
}
```

### DAO 查询方法（PostQuery）

| 方法 | SQL 操作 | 说明 |
|---|---|---|
| `QueryPostById(postId)` | SELECT * WHERE id=? | 按 ID 获取完整文章 |
| `QueryPostByTitle(postName)` | SELECT * WHERE title=? | 按标题获取完整文章 |
| `QueryPostIdByTitle(postName)` | SELECT id WHERE title=? | 仅获取 ID |
| `QueryPostDataProfile(pageIdx, perPageNum)` | SELECT 部分列 ORDER BY created_at DESC LIMIT/OFFSET | 首页分页列表 |
| `QueryPostDataProfileByCategoryId(categoryId, pageIdx, perPageNum)` | SELECT 部分列 WHERE category_id=? ORDER BY created_at DESC | 分类下的分页列表 |
| `QueryPostsDataProfileSinceLastByPageDesc(lastId, perPageNum)` | SELECT 部分列 WHERE id>lastId ORDER BY id DESC LIMIT | 游标分页（向前翻页） |
| `QueryPostsDataWithCategoryProfileSinceLastByPageDesc(lastId, categoryId, perPageNum)` | SELECT 部分列 WHERE id>lastId AND category_id=? LIMIT | 分类下的游标分页 |
| `QueryPostsBasicStatusByPage(lastId, perPageNum)` | SELECT 部分列 WHERE id>lastId ORDER BY id DESC LIMIT | 后台文章管理列表 |
| `InsertPost(post)` | INSERT（跳过自增 id） | 新增文章 |
| `UpdatePostById(postId, post)` | UPDATE SET title,category_id,profile,titlepic,content,updated_at WHERE id=? | 修改文章 |
| `DeletePostById(postId)` | DELETE WHERE id=? | 删除文章 |
| `updateViewTimes(id)` | UPDATE SET view_times=view_times+1 WHERE id=? | 阅读计数 +1 |
| `QueryPostCount()` | SELECT COUNT(*) | 文章总数 |
| `QueryPostOfCategoryCount(id)` | SELECT COUNT(*) WHERE category_id=? | 某分类下文章总数 |

---

## 表 2：Categories（分类表）

### DDL

```sql
CREATE TABLE "Categories" (
    "id"          INTEGER NOT NULL UNIQUE,
    "name"        TEXT    NOT NULL UNIQUE,
    "categorypic" TEXT    NOT NULL,
    PRIMARY KEY("id" AUTOINCREMENT)
);
```

### 列说明

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| id | INTEGER | PK, AUTOINCREMENT, NOT NULL | 分类唯一 ID |
| name | TEXT | NOT NULL, UNIQUE | 分类名称（唯一） |
| categorypic | TEXT | NOT NULL | 分类图片路径 |

### C++ 模型

```cpp
namespace Blog::Model {
class Category {
public:
  uint64_t id;
  std::string name;
  std::string categorypic;
  ENTITY_TABLE(Categories);
  AUTO_INC_PK(id);
};
}
```

### DAO 查询方法（CategoryQuery）

| 方法 | SQL 操作 | 说明 |
|---|---|---|
| `QueryCategoryById(categoryId)` | SELECT * WHERE id=? | 按 ID 获取分类 |
| `QueryCategoryNameById(categoryId)` | SELECT name WHERE id=? | 按 ID 获取分类名 |
| `QueryCategoryBasicInfoById(categoryId)` | SELECT id, name WHERE id=? | 按 ID 获取基本信息 |
| `QueryCategoryBasicInfo()` | SELECT id, name | 获取所有分类基本信息 |
| `QueryAllCategoryBasicInfo()` | SELECT id, name FROM "Categories" | 获取所有分类基本信息（显式 FROM） |
| `QueryAllCategory()` | SELECT * FROM "Categories" | 获取所有完整分类 |
| `InsertCategory(category)` | INSERT（跳过自增 id） | 新增分类 |
| `UpdateCategory(category)` | UPDATE SET name, categorypic WHERE id=? | 修改分类 |
| `DeleteCategory(categoryId)` | DELETE WHERE id=? | 删除分类 |

---

## 表 3：BlogStatus（键值状态表）

### DDL

```sql
CREATE TABLE "BlogStatus" (
    "name" TEXT NOT NULL,
    "data" TEXT,
    PRIMARY KEY("name")
);
```

### 列说明

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| name | TEXT | PK, NOT NULL | 键名（主键） |
| data | TEXT | - | 值（字符串） |

### C++ 模型

```cpp
namespace Blog::Model {
struct BlogStatus {
  std::string name;
  std::string data;
  ENTITY_TABLE(BlogStatus)
  // 注意：没有 AUTO_INC_PK，主键是 name
};
}
```

### DAO 查询方法（BlogStatusQuery）

| 方法 | SQL 操作 | 说明 |
|---|---|---|
| `queryBlogStatus(propertyName)` | SELECT data WHERE name=? | 按键名获取值，无匹配时返回空字符串 |

**用途**：存储博客级别的键值配置/状态，如总访问量计数器等。

---

## 表 4：TestMessage（测试留言表）

### DDL

```sql
CREATE TABLE "TestMessage" (
    "id"         INTEGER NOT NULL,
    "name"       TEXT    NOT NULL,
    "content"    TEXT    NOT NULL,
    "created_at" INTEGER,
    PRIMARY KEY("id" AUTOINCREMENT)
);
```

### 列说明

| 列 | 类型 | 约束 | 说明 |
|---|---|---|---|
| id | INTEGER | PK, AUTOINCREMENT, NOT NULL | 消息 ID |
| name | TEXT | NOT NULL | 留言者名称 |
| content | TEXT | NOT NULL | 留言内容 |
| created_at | INTEGER | - | 创建时间（Unix 时间戳） |

### C++ 模型

```cpp
namespace Blog {
class TestMessage {
public:
  uint64_t id;
  std::string name;
  std::string content;
  uint64_t created_at;
  ENTITY_TABLE(TestMessage)
  AUTO_INC_PK(id)
};
}
```

### DAO 查询

此表未定义独立的 `*Query` 类。数据访问通过 `DBTestController` 直接调用 `Query<TestMessage>` 模板操作完成（INSERT、按页 SELECT）。

---

## 预留模型（仅 C++ 定义，DDL 无对应表）

### Tags（标签）

```cpp
namespace Blog::Model {
class Tag {
public:
  uint64_t id;
  std::string name;
  ENTITY_TABLE(Tags)   // ← 表中无此 DDL
};
}
```

目前未使用。Posts 表中的 `tags` 字段为逗号分隔字符串，未关联此模型。

### Users（用户）

```cpp
namespace Blog::Model {
class User {
public:
  uint64_t id;
  std::string username;
  std::string email;
  std::string password_hash;
  ENTITY_TABLE(Users)  // ← 表中无此 DDL
};
}
```

### UserQuery 方法

| 方法 | SQL 操作 | 说明 |
|---|---|---|
| `queryUserAndPasswordHash(userName, hash)` | SELECT id WHERE username=? AND password_hash=? | 验证用户名和密码哈希，存在匹配行返回 `true` |

注意：当前 `DbCreateSql.txt` 中未定义 Users 表。如果未来需要启用用户认证（替代当前配置文件中的硬编码凭据），需添加对应 DDL。

---

## 数据库文件运行时状态

```
Server/resources/database/
├── blog.db          # 主数据库文件
├── blog.db-shm      # WAL 共享内存（运行时自动创建）
└── blog.db-wal      # WAL 日志文件（运行时自动创建）
```

非正常关闭（如 kill -9）可能导致 `.db-shm` 和 `.db-wal` 文件残留，服务器启动时 SQLite 会自动恢复。
