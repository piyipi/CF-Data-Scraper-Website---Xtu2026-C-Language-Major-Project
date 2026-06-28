# http_client —— HTTP 网络层

**文件**：`src/http_client.h`（27 行）+ `src/http_client.c`（91 行）

**角色**：整个项目唯一与外界通信的模块。对 libcurl 做最小封装，提供三种操作：初始化、发送 GET 请求、清理。

---

## 一、对外接口

| 函数 | 功能 | 调用时机 |
|------|------|----------|
| `http_init()` | 全局初始化 libcurl（程序生命周期仅一次） | `main()` 入口第一行 |
| `http_cleanup()` | 全局清理 libcurl 资源 | `main()` return 前最后一行 |
| `http_get(url, &response)` | 发送 GET 请求，返回响应体 | `cf_api.c` 中每个 API 调用 |

---

## 二、数据结构

### 2.1 HttpResponse（对外）

```c
typedef struct {
    char  *data;   // 响应体内容（以 \0 结尾）
    size_t size;   // 响应体长度（不含 \0）
} HttpResponse;
```

| 字段 | 说明 |
|------|------|
| `data` | 指向 malloc 分配的堆内存，存储完整 HTTP 响应体。末尾自动补了一个 `\0`，因此可以安全地传给 `cJSON_Parse` 直接当作 C 字符串使用。 |
| `size` | 响应体的真实字节数（不含末尾 `\0`）。用于调试日志，实际 JSON 解析不需要它（因为有 `\0`）。 |

**内存所有权**：`data` 由 http_client 的 write_callback 用 realloc 分配，成功返回后所有权转移给调用者。调用者用完后必须 `free(resp.data)`，否则泄漏。

### 2.2 WriteCtx（内部）

```c
struct WriteCtx {
    char  *data;
    size_t size;
};
```

内部辅助结构，不对外暴露：

- `data`：指向**正在累积的响应缓冲区**（realloc 管理）
- `size`：当前已累积的字节数

它通过 `CURLOPT_WRITEDATA` 传给 libcurl，libcurl 每收到一块 HTTP 响应数据就回调 `write_callback`，并把 `WriteCtx` 作为 userdata 传入。

---

## 三、函数实现原理

### 3.1 write_callback（内部）

```c
static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
```

**工作流程**：

```
libcurl 收到一块数据（例如 4096 字节）
        │
        ▼
write_callback(ptr=数据起始地址, size=1, nmemb=4096, userdata=&ctx)
        │
        ├─ 计算实际长度: len = size * nmemb (= 4096)
        │
        ├─ realloc(ctx->data, ctx->size + len + 1)  ← 扩展缓冲区（+1 留给 \0）
        │       └─ 如果 realloc 失败（返回 NULL），返回 0 通知 libcurl 中断传输
        │
        ├─ memcpy 将新数据追加到缓冲区尾部
        │
        ├─ ctx->size += len  ← 更新累积长度
        │
        ├─ ctx->data[ctx->size] = '\0'  ← 关键：每次都补 \0，保证缓冲区始终是合法 C 字符串
        │
        └─ 返回 len → 告诉 libcurl "我成功处理了这么多字节"
```

**关键设计点**：

1. **每次回调都补 `\0`**：不是最后一次性补，而是每次追加新数据后立刻补。这样做的好处是——即使 libcurl 中途出错（但回调仍被调用），缓冲区也是一段合法的 C 字符串，不会在错误处理中读到乱码。

2. **realloc 而非固定缓冲区**：CF API 返回的 JSON 从几百字节到几十 KB 不等（tourist 的 user.status 约 50KB）。使用 realloc 动态增长可以适应各种大小，不会浪费内存也不会溢出。

3. **返回 0 中断**：libcurl 规定 write_callback 返回 0 表示"我无法处理更多数据"，libcurl 会停止传输并返回错误。这里仅在 realloc 失败（OOM）时返回 0。

### 3.2 g_initialized 静态标志

```c
static int g_initialized = 0;
```

libcurl 规定 `curl_global_init` 在整个进程生命周期中**只能调用一次**（多线程环境需额外注意），`curl_global_cleanup` 同理。此标志是防御性设计——万一将来代码重构导致多次调用，不会触发 libcurl 未定义行为。

```
http_init()    → if (!g_initialized) { curl_global_init(); g_initialized = 1; }
http_cleanup() → if (g_initialized)  { curl_global_cleanup(); g_initialized = 0; }
```

### 3.3 http_init

```c
void http_init(void)
```

调用 `curl_global_init(CURL_GLOBAL_ALL)`，初始化 libcurl 所有子系统：SSL/TLS、DNS 解析器等。这是 `main()` 中第一个被调用的子模块函数。

### 3.4 http_cleanup

```c
void http_cleanup(void)
```

调用 `curl_global_cleanup()`，释放 libcurl 内部持有的所有资源（SSL 上下文、DNS 缓存等）。注意：只清理 libcurl 的全局状态，不负责释放单个请求的 `HttpResponse.data`——那由调用者在每次请求后自行 free。

### 3.5 http_get（核心）

```c
int http_get(const char *url, HttpResponse *response)
```

**功能**：发送一个 HTTP GET 请求，将响应体存入 response。

**完整执行流程**：

```
① curl_easy_init()
        创建 CURL 句柄（类似 socket 创建）
        │
② curl_easy_setopt(curl, CURLOPT_URL, url)
        设置目标 URL，例如 "https://codeforces.com/api/user.info?handles=tourist"
        │
③ curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback)
        注册回调函数
        │
④ curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx)
        传递 WriteCtx 指针
        │
⑤ curl_easy_setopt(curl, CURLOPT_USERAGENT, "CF-Crawler/1.0")
        设置 User-Agent，标识客户端身份
        │
⑥ curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L)
        30 秒超时（连接 + 数据传输总和）
        │
⑦ curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L)
        自动跟随 HTTP 3xx 重定向
        │
⑧ curl_easy_perform(curl)
        阻塞执行网络请求：DNS 解析 → TCP+TLS → GET → 接收响应 → 回调 write_callback
        │
⑨ curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status)
        获取 HTTP 状态码
        │
⑩ 如果 status == 200：
        response->data = ctx.data    ← 转移数据所有权
        response->size = ctx.size
        ctx.data = NULL              ← 防止 cleanup 时 double-free
        返回 0（成功）
    否则：
        fprintf(stderr, ...)         ← 输出错误日志
        free(ctx.data)               ← 释放无用数据
        返回 -1（失败）
        │
⑪ curl_easy_cleanup(curl)
        释放 CURL 句柄（类似 socket 关闭）
```

**三种失败路径**：

| 失败类型 | 检测方式 | 示例 |
|---------|---------|------|
| 网络/协议错误 | `curl_easy_perform` 返回非 `CURLE_OK` | DNS 失败、连接超时、TLS 错误 |
| HTTP 非 200 | 状态码 != 200 | CF API 返回 400（无效 handle）、503（服务不可用） |
| 参数无效 | url 或 response 为 NULL | 直接返回 -1 |

**HTTP 配置参数汇总**：

| 参数 | 值 | 作用 |
|------|-----|------|
| `CURLOPT_TIMEOUT` | 30 秒 | 防止 CF API 卡死时无限等待 |
| `CURLOPT_FOLLOWLOCATION` | 1（开启） | 跟随 HTTP 重定向 |
| `CURLOPT_USERAGENT` | `"CF-Crawler/1.0"` | 标识自身身份 |

---

## 四、调用关系

```
main.c
  ├── http_init()                    ← 程序启动时
  │
  ├── [处理过程中，cf_api.c 多次调用]
  │     └── cf_api.c: api_get()
  │           └── http_get(url, &resp)   ← 每个 CF API 请求调用一次
  │                 ├── 成功 → 返回 resp.data（JSON 文本）
  │                 └── 失败 → 打印 stderr，返回 NULL
  │
  └── http_cleanup()                 ← 程序退出前
```

---

## 五、设计评价

- **薄封装，只做一件事**：无并发、无线程，保持极简
- **内存约定明确**：`data` 由调用者释放，所有权转移通过 `ctx.data = NULL` 防止 double-free
- **重试策略**：3 次重试 + 429 专用 2s 等待 + 通用 1s 等待，适配 CF API 限流特性
- **幂等保护**：init/cleanup 均通过 g_initialized 标志防护，重复调用安全
- **无配置抽象**：libcurl 参数直接硬编码，对 CF API 这一特定场景足够
- **每次重试新建句柄**：避免旧会话状态（SSL session/cookie）污染新请求
