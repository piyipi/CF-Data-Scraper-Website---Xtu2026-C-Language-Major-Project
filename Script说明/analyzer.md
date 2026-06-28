# analyzer —— 数据分析与导出层

**文件**：`src/analyzer.h`（12 行）+ `src/analyzer.c`（278 行）

**角色**：项目的"大脑"。负责三步聚合——拉取 API 数据、统计分析、导出 JS 文件。是所有业务逻辑的集中地。

---

## 一、对外接口

| 函数 | 功能 | 调用方 |
|------|------|--------|
| `analyze_user(handle, &ud)` | 聚合分析单个用户 | main.c |
| `export_data_js(&ud, filepath)` | 将 UserData 导出为 JS 文件 | main.c |

---

## 二、analyze_user —— 核心分析函数

```c
int analyze_user(const char *handle, UserData *out)
```

这是整个项目最核心的函数。输入一个 CF handle，输出填充完整的 UserData。执行流程分为**六个阶段**。

### 阶段一：基本信息获取

```
cf_get_user_info(handle, &out->info)
        │
        ├─ 成功：UserInfo 填充完成（handle、rating、max_rating、rank_name、avatar_url、cf_color）
        └─ 失败：fprintf(stderr)，return -1（分析终止）
```

### 阶段二：比赛历史获取

```
cf_get_user_ratings(handle, &out->records, &out->record_count)
        │
        ├─ 成功：out->records 指向 malloc 分配的 ContestRecord 数组
        │        out->record_count 记录数组长度
        │        out->contest_count = out->record_count（总比赛次数）
        └─ 失败：userdata_free(out)，return -1
```

**注意**：records 数组按 CF API 返回的顺序排列——**从远到近**（最早比赛在 records[0]）。后续统计和导出都依赖此顺序。

### 阶段三：提交记录获取

```
cf_get_user_status(handle, &status_root)
        │
        ├─ 成功：status_root = cJSON 树，提取 result 数组
        └─ 失败：status_arr = NULL（后续补题和难度统计将跳过，但不影响基本信息输出）
```

**注意**：即使 `user.status` 拉取失败，也不中断分析——用户的基本信息和比赛历史仍然有价值。补题匹配和难度直方图会在 `status_arr == NULL` 时自动跳过。user.status 采用分页拉取（1000条/页，最多 10000 条）。

### 阶段四：统计 + 补题匹配（核心循环）

```c
for (int i = 0; i < out->record_count; i++) {
    ContestRecord *rec = &out->records[i];
    int cid = rec->contest_id;
    time_t cutoff = rec->start_time;  // ← Rating 更新时间 = 赛时/赛后分界线
```

#### 4A：近 180 天统计

```
days_since(rec->start_time) <= 180 ?
        ├─ 是：out->recent_count_180d++
        └─ 是 且 rec->new_rating > out->recent_max_rating_180d：
                  out->recent_max_rating_180d = rec->new_rating
```

#### 4B：赛时/赛后提交判别

核心逻辑用 **ProbState 临时结构体** 跟踪每道题的状态：

```c
struct ProbState {
    char   index[4];      // 题号
    int    attempts;      // 赛时提交次数
    double points;        // 赛时最高分
    int    best_time;     // 赛时首次通过时间（秒，距 cutoff）
    int    upsolved;      // 赛后补题标记（0/1）
};
```

**遍历提交记录，匹配当前比赛**：

```
for (每条提交 j) {
    if (提交的 contestId != cid) continue;   ← 过滤不属于当前比赛的提交
    if (提交不存在 problem 字段) continue;

    提取：题号(pid) / verdict(v) / 提交时间(sub_time)

    if (sub_time <= cutoff) {   ← 赛时提交
        ps[题号].attempts++;
        ps[题号].points = max(ps[题号].points, 本次得分);
        // 得分规则：有 points 字段取 points，无 points 字段按 OK 给 1.0
        if (is_ok && ps[题号].best_time == 0)
            ps[题号].best_time = sub_time - cutoff;  // 记录首次通过时间
    } else {                    ← 赛后提交
        if (is_ok)
            ps[题号].upsolved = 1;   // 通过即标记为补题
    }
}
```

**评分规则的兼容性**：
- CF 旧赛制（ICPC）：无 points 字段 → OK 给 1.0，非 OK 给 0
- CF 新赛制（分值）：有 points 字段 → 直接用 API 返回的分值
- 多次提交 → 取最高分（`if (pts > ps[pi].points) ps[pi].points = pts`）

#### 4C：回填到 ContestRecord

```
for (每个遇到的题号 k) {
    rec->problems[rec->problem_count++] = {
        .index = ps[k].index,
        .points = ps[k].points,
        .attempt_count = ps[k].attempts,
        .best_time = ps[k].best_time ? ps[k].best_time : -1   // 未通过记为 -1
    };
    
    if (ps[k].upsolved) {
        rec->upsolved[rec->upsolved_count++] = ps[k].index;   // 记录补做题号
    }
}
```

### 阶段五：难度直方图统计

```c
for (每条提交 j) {
    if (verdict != "OK") continue;           ← 只统计通过的题目
    if (problem 无 rating 字段) continue;    ← 无难度的题（如未评级的比赛）跳过

    int diff = rating;
    int bucket = (diff - 800) / 100;          ← 难度 → 桶映射
    if (bucket < 0) bucket = 0;
    if (bucket >= 28) bucket = 27;

    计算 days_ago = (now - sub_time) / 86400.0

    histogram[0][bucket]++;                    ← 全部
    if (days_ago <= 365) histogram[1][bucket]++;
    if (days_ago <= 180) histogram[2][bucket]++;
    if (days_ago <= 30)  histogram[3][bucket]++;
}
```

**为什么直接手动计算而非调用 days_since**：在内层循环中预先获取一次 `now = time(NULL)` 并复用，避免每次调用 days_since 都执行 `time(NULL)` 系统调用。

**注意**：同一条提交如果在多个时段范围内，会被同时计入多个直方图（例如 30 天前的提交同时计入"全部"、"365天"、"180天"，但不计入"30天"）。这不是 bug——"全部"是"30天"的超集，各时段独立展示。

### 阶段六：清理

```
cJSON_Delete(status_root);     ← 释放提交记录的 JSON 树
return 0;
```

**为什么在最后才释放 status_root**：因为阶段四的补题匹配和阶段五的难度统计都需要遍历相同的提交记录。如果提前释放，需要二次拉取 API 或缓存所有字段——当前设计一次遍历完成两项分析，更高效。

---

## 三、export_data_js —— JS 文件导出

```c
int export_data_js(const UserData *ud, const char *filepath)
```

**功能**：将 UserData 导出为 `var CF_DATA = { ... };` 格式的 JavaScript 文件，供前端 HTML 通过 `<script src="data.js">` 直接加载。

### 输出的 JS 数据结构

```javascript
var CF_DATA = {
  handle: "tourist",
  rating: 3774,
  maxRating: 4009,
  rank: "legendary grandmaster",
  avatar: "https://userpic.codeforces.org/...jpg",
  color: "#FF0000",
  contestCount: 350,
  recentCount180d: 12,
  recentMaxRating180d: 3774,

  ratingHistory: [                    // 从近到远排列
    {
      contestName: "Codeforces Round #...",
      time: 1700000000,              // Unix 时间戳
      rank: 1,
      oldRating: 3774,
      newRating: 3790,
      points: 8000,
      problems: [                     // 赛时各题
        { index: "A", points: 500,  attempts: 1, bestTime: 120 },
        { index: "B", points: 1000, attempts: 2, bestTime: 600 },
        { index: "C", points: 0,    attempts: 3, bestTime: -1 },   // 未通过
        ...
      ],
      upsolved: ["C"]                // 补题题号
    },
    ...
  ],

  histogram: {
    min: 800,
    step: 100,
    labels: ["800-899", "900-999", ..., "3400-3499", "3500+"],
    periods: [
      { name: "All Time",  data: [0, 5, 12, ...] },
      { name: "365 Days",  data: [0, 3, 8,  ...] },
      { name: "180 Days",  data: [0, 2, 5,  ...] },
      { name: "30 Days",   data: [0, 1, 2,  ...] },
    ]
  }
};
```

### 关键实现细节

#### 字符串转义（escape_json_str）

```c
static void escape_json_str(const char *src, char *dst, size_t dst_size)
```

手动处理 5 种 JS 字符串转义：`\` → `\\`、`"` → `\"`、`\n` → `\\n`、`\r` → `\\r`、`\t` → `\\t`。比赛名称和头像 URL 经此函数处理后写入 JS 文件，确保不会破坏 JS 语法。

#### 比赛历史倒序输出

```c
for (int i = ud->record_count - 1; i >= 0; i--)
```

CF API 返回的比赛按时间从远到近排列，但前端展示需要从近到远。在导出 JS 时倒序遍历即可，无需修改原始数组。

#### 为什么用 fprintf 而非 cJSON

cJSON 提供了 `cJSON_Print` / `cJSON_PrintUnformatted` 来构造 JSON，但本项目选择直接用 `fprintf` 拼接 JS。原因：

1. **控制权**：精确控制输出格式（缩进、逗号位置），确保生成的人类可读 JS
2. **性能**：避免先构造 cJSON 树再输出的中间步骤（histogram 数组含 4×28=112 个元素）
3. **特殊变量名**：`var CF_DATA = ...` 是 JS 变量声明，而非纯 JSON

---

---

## 四、在项目架构中的位置

```
main.c ──调用──▶ analyze_user(handle, &ud)
        │             │
        │             ├─ cf_get_user_info()     → 基本信息
        │             ├─ cf_get_user_ratings()  → 比赛历史
        │             ├─ cf_get_user_status()   → 提交记录
        │             ├─ [内部循环]              → 补题匹配 + 难度统计
        │             └─ return UserData
        │
        └──调用──▶ export_data_js(&ud, "output/data.js")
                      └─ fprintf → data.js（JS 变量格式）
```

---

## 五、设计特点

| 设计点 | 说明 |
|--------|------|
| 一次遍历多项统计 | 提交记录遍历一次，同时完成补题匹配和难度直方图两项分析 |
| 失败不中断 | user.status 拉取失败不影响基本信息输出，仅跳过补题和直方图 |
| 新旧赛制兼容 | 赛时得分通过 points 字段是否存在的启发式判断，无需显式区分赛制 |
| 时序分界线 | 以 ratingUpdateTimeSeconds 为赛时/赛后分界，而非比赛开始时间——与 CF 实际行为一致 |
| fprintf 直接输出 | 不用 cJSON 转 JSON，直接拼接 JS 变量声明，保证输出格式和性能 |
