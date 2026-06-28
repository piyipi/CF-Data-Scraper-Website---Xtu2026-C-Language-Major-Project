# cf_api —— Codeforces API 调用层

**文件**：`src/cf_api.h`（28 行）+ `src/cf_api.c`（382 行）

**角色**：封装 3 个 Codeforces API 接口的调用逻辑。上层只需传入 handle，即可获得填充好的 C 结构体或原始 JSON 树。

---

## 一、对外接口

| 函数 | API | 返回类型 | 调用方 |
|------|-----|---------|--------|
| `cf_get_user_info(handle, &info)` | `user.info` | UserInfo 结构体 | analyzer.c |
| `cf_get_user_ratings(handle, &records, &count)` | `user.rating` | ContestRecord[] 动态数组 | analyzer.c |
| `cf_get_user_status(handle, &root)` | `user.status` | cJSON 原始对象（分页合并） | analyzer.c |

所有 API 都基于 `https://codeforces.com/api/` 基础 URL（宏 `CF_API_BASE`）。

---

## 二、内部公共函数：api_get

```c
static cJSON *api_get(const char *method, const char *params)
```

**角色**：所有 3 个公开函数的内部基石。负责 URL 组装 → HTTP 请求 → JSON 解析 → 状态校验的四步流水线。

### 执行流程

```
① snprintf(url, sizeof(url), "%s/%s?%s", CF_API_BASE, method, params)
        组装完整 URL，例如：
        "https://codeforces.com/api/user.info?handles=tourist"
                 │                │           │
            CF_API_BASE        method      params
        │
② http_get(url, &resp)
        调用 http_client 发送 GET 请求
        ├─ 成功：resp.data 指向响应 JSON 文本
        └─ 失败：fprintf(stderr, ...)，返回 NULL
        │
③ cJSON_Parse(resp.data)
        解析 JSON 文本为 cJSON 树
        │
④ free(resp.data)
        立即释放 HTTP 响应体——JSON 数据已转入 cJSON 树，不再需要原始文本
        │
⑤ 校验 JSON 顶层结构：
        cJSON_GetObjectItem(root, "status")
        └─ status != "OK" → fprintf(stderr, CF API 错误信息)，cJSON_Delete(root)，返回 NULL
        │
⑥ 返回 root（cJSON *）
        调用者需 cJSON_Delete(root)
```

### CF API 通用响应格式

```json
{
  "status": "OK",       // 或 "FAILED"
  "comment": "...",     // 仅失败时有，错误描述
  "result": { ... }     // 实际数据
}
```

`api_get` 统一校验了 `status == "OK"`，失败时从 `comment` 字段提取错误信息输出到 stderr。这意味着上层 3 个函数拿到 root 后无需再检查 status，直接操作 `result` 即可。

### 错误处理矩阵

| 失败点 | 表现 | 返回值 |
|--------|------|--------|
| URL 拼接溢出（>1024 字节） | fprintf + return NULL | NULL |
| HTTP 请求失败（网络/超时） | http_get 内部 fprintf + return NULL | NULL |
| JSON 解析失败（非 JSON 响应） | fprintf + return NULL | NULL |
| API status != "OK" | fprintf（附带 CF API 的 comment）+ cJSON_Delete + return NULL | NULL |

每一步失败都会**自动清理已分配的资源**（resp.data 在进入 JSON 解析前就 free 了，root 在发现错误时 cJSON_Delete），无内存泄漏。

---

## 三、三个公开函数的实现原理

### 3.1 cf_get_user_info —— 获取用户基本信息

```c
int cf_get_user_info(const char *handle, UserInfo *info)
```

**API**：`user.info?handles={handle}`

**API 响应格式**：

```json
{
  "status": "OK",
  "result": [                            ← 数组，通常只有一个元素
    {
      "handle": "tourist",
      "rating": 3774,
      "maxRating": 4009,
      "rank": "legendary grandmaster",   ← 注意：这里是 CF 返回的官方头衔名称
      "avatar": "https://userpic.codeforces.org/...jpg",
      "firstName": "...",
      "lastName": "...",
      "country": "...",
      ...
    }
  ]
}
```

**处理流程**：
```
① api_get("user.info", "handles=tourist") → root
        │
② cJSON_GetObjectItem(root, "result") → arr
        校验 arr 是数组且非空
        │
③ cJSON_GetArrayItem(arr, 0) → user
        取第一个元素（通常也是唯一的）
        │
④ 逐个字段提取：
        handle      → snprintf(info->handle, ...)
        rating      → info->rating      (int)
        maxRating   → info->max_rating  (int)
        rank        → snprintf(info->rank_name, ...)
        avatar      → snprintf(info->avatar_url, ...)
        │
⑤ snprintf(info->cf_color, ..., "%s", cf_color(info->rating))
        通过 utils 的颜色映射函数计算当前等级分颜色（非 API 返回）
        │
⑥ cJSON_Delete(root) + return 0
```

**关键设计**：虽然 API 的 `rank` 字段已包含头衔名称，但 `cf_color` 仍通过 `utils.cf_color()` 独立计算——不依赖 API 返回的颜色信息（CF API 实际上不直接返回颜色）。这保证了颜色规则与 CF 官网完全一致。

### 3.2 cf_get_user_ratings —— 获取比赛 Rating 历史

```c
int cf_get_user_ratings(const char *handle, ContestRecord **records, int *count)
```

**API**：`user.rating?handle={handle}`

**API 响应格式**：
```json
{
  "status": "OK",
  "result": [
    {
      "contestId": 1000,
      "contestName": "Codeforces Round #100",
      "ratingUpdateTimeSeconds": 1234567890,
      "oldRating": 1500,
      "newRating": 1600,
      "rank": 500,
      "points": 1500.0
    },
    ...
  ]
}
```

**处理流程**：
```
① api_get("user.rating", "handle=tourist") → root
        │
② cJSON_GetObjectItem(root, "result") → arr
        校验 arr 是数组
        │
③ n = cJSON_GetArraySize(arr)
        如果 n == 0：cJSON_Delete(root)，return 0（无比赛也算成功）
        │
④ *records = malloc(n * sizeof(ContestRecord))
        一次性分配 n 个 ContestRecord 的连续内存块
        │
⑤ 遍历 arr 的每个元素，填充 ContestRecord：
        for (i = 0; i < n; i++) {
            memset(rec, 0, sizeof(ContestRecord));   ← 先清零
            contestId              → rec->contest_id
            contestName            → snprintf(rec->contest_name, ...)
            ratingUpdateTimeSeconds → rec->start_time  ← 注意：这不是比赛开始时间，是 Rating 更新时间
            oldRating              → rec->old_rating
            newRating              → rec->new_rating
            rank                   → rec->rank
            points                 → rec->points
        }
        │
⑥ *count = n; cJSON_Delete(root); return 0
```

**内存管理**：

- `*records` 由 malloc 分配，调用者需 free（通过 `userdata_free` → `free(ud->records)`）
- `*count` 为输出参数，指示数组长度
- 数组按 CF API 返回顺序排列（**从远到近**，与前端展示相反——前端在 `export_data_js` 中倒序输出）

**重要：start_time 的语义**：API 返回的 `ratingUpdateTimeSeconds` 严格来说是"Rating 更新时间"，通常等于比赛结束时间（Codeforces 在比赛结束后立即更新 Rating）。但分析模块将其同时用作：

1. 比赛时间线定位
2. 赛时/赛后提交的分界线（时间戳 ≤ start_time 视为赛时）
3. 近 180 天筛选的基准

### 3.3 cf_get_user_status —— 获取用户提交记录（分页合并）

```c
int cf_get_user_status(const char *handle, cJSON **root)
```

**API**：`user.status?handle={handle}&from=1&count=1000`

**分页策略**：`CF_STATUS_PAGE_SIZE` 宏定义为 1000 → 先拉第 1 页（from=1），若满页（1000 条）且未达 10000 硬上限 → `Sleep(1000)` 遵守 CF 频率限制 → 继续拉下一页 → 用 `cJSON_DetachItemFromArray` + `cJSON_AddItemToArray` 逐条移入第 1 页的 result 数组 → `cJSON_Delete` 释放分页临时树 → 循环直到不满页或达到 10000 上限。最多 10 页 × 1000 条 = 10000 条提交。

**API 响应格式**（单页）：
```json
{
  "status": "OK",
  "result": [
    {
      "id": 123456789,
      "contestId": 1000,
      "problem": {
        "contestId": 1000,
        "index": "A",
        "name": "...",
        "rating": 800               ← 题目难度等级分
      },
      "verdict": "OK",               ← 提交结果
      "points": 1.0,
      "creationTimeSeconds": 1234567890
    }
  ]
}
```

**处理流程**：
```
① api_get("user.status", "handle=tourist&from=1&count=1000") → res
    │
② 取 result_arr = cJSON_GetObjectItem(res, "result")
    │
③ while: page_items >= 1000 && from + 1000 - 1 <= 10000
    ├─ Sleep(1000)              ← CF 频率限制
    ├─ api_get 下一页
    ├─ 取 page_arr
    ├─ while(page_arr 非空):
    │     item = cJSON_DetachItemFromArray(page_arr, 0)
    │     cJSON_AddItemToArray(result_arr, item)
    └─ cJSON_Delete(page) + from += 1000
    │
④ *root = res; return 0
    ← 所有页面合并入第 1 页的 result 数组, 返回合并后的 cJSON 树
```

**为什么不提取字段**：提交记录需结合 ContestRecord 时间戳做赛时/赛后判别、补题匹配、难度直方图统计，逻辑复杂且涉及多模块数据。因此返回原始 JSON 树，由 `analyzer.c` 在一次遍历中完成全部分析，避免两次遍历。

**硬上限**：最多 10000 条 —— 对应 10 页 × 1000 条。对提交量极大的用户（5000+），较早的提交仍可能丢失，但对"近 180 天"分析通常足够覆盖。

---

## 四、在项目架构中的位置

```
analyzer.c ──调用──▶ cf_get_user_info()
                    cf_get_user_ratings()
                    cf_get_user_status()
        │
        ▼
cf_api.c: api_get(method, params)
        │
        ├─ snprintf → 组装 URL
        ├─ http_get → 网络请求
        ├─ cJSON_Parse → JSON 解析
        ├─ 校验 status == "OK"
        └─ 返回 cJSON 树 / 填充结构体
```

---

## 五、API 端点一览

| 接口 | URL | 参数 | 用途 |
|------|-----|------|------|
| user.info | `/api/user.info` | `?handles={handle}` | 用户名、等级分、头衔、头像 |
| user.rating | `/api/user.rating` | `?handle={handle}` | 比赛 Rating 变化历史 |
| user.status | `/api/user.status` | `?handle={handle}&from=1&count=1000` | 分页拉取提交记录（最多 10 页 × 1000 = 10000 条） |

---
---

## 六、设计特点

| 设计点 | 说明 |
|--------|------|
| 统一 api_get 基座 | 3 个公开函数复用同一 URL 组装 + 请求 + 解析 + 校验管线，减少重复代码 |
| 两态返回 | 简单接口（user.info, user.rating）直接填充结构体；复杂接口（user.status）返回原始 JSON 树，由上层灵活处理 |
| 连续内存分配 | user.rating 用一次 malloc 分配所有 ContestRecord，而非逐条分配——遍历更快、释放更简单 |
| 按顺序填充 | memset(rec, 0, ...) 确保未赋值的字段为零值，避免未初始化的内存出现在输出中 |
| 分页合并 | user.status 自动分页拉取（1000 条/页），Sleep(1000) 遵守 CF 频率限制，上限 10000 条 |
