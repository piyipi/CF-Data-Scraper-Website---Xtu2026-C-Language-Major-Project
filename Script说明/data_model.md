# data_model —— 数据结构层

**文件**：`src/data_model.h`（61 行）+ `src/data_model.c`（10 行）

**角色**：项目的"类型骨架"。定义所有核心数据结构及一个内存释放函数。整个项目的 C 代码都在围绕这些结构体工作。

---

## 一、数据结构全景

```
UserData（顶层聚合）
 ├── UserInfo info                 用户基本信息
 │     ├── handle[64]              用户名
 │     ├── rating / max_rating     当前等级分 / 历史最高分
 │     ├── rank_name[32]           头衔（如 "Expert"）
 │     ├── avatar_url[256]         头像图片 URL
 │     └── cf_color[16]            等级分对应色值（如 "#0000FF"）
 │
 ├── contest_count                 历史总比赛次数
 ├── recent_count_180d             近 180 天比赛次数
 ├── recent_max_rating_180d        近 180 天最高等级分
 │
 ├── records → ContestRecord[]     比赛记录数组（动态分配）
 │     ├── contest_id              比赛 ID
 │     ├── contest_name[256]       比赛名称
 │     ├── start_time              开始时间（Unix 时间戳）
 │     ├── old_rating / new_rating 赛前/赛后等级分
 │     ├── rank / points           排名 / 总分
 │     └── problems → ProblemResult[]  各题详情（最多 12 题）
 │           ├── index[4]          题号（"A", "B1", "C"...）
 │           ├── points            该题得分
 │           ├── attempt_count     提交次数（含失败）
 │           └── best_time         首次通过时间（秒，距比赛开始），未通过则为 -1
 │
 ├── record_count                  比赛记录数量
 │
 └── histogram[4][28]              难度直方图
       ├── [0][*] 全部时间
       ├── [1][*] 近 365 天
       ├── [2][*] 近 180 天
       └── [3][*] 近 30 天
```

---

## 二、结构体定义详解

### 2.1 UserInfo（用户基本信息）

```c
typedef struct {
    char handle[64];        // 用户名
    int  rating;            // 当前等级分（未参与过比赛的为 0）
    int  max_rating;        // 历史最高等级分
    char rank_name[32];     // 头衔，CF 官方命名如 "Expert"、"Grandmaster"
    char avatar_url[256];   // 头像图片地址，通常是 CF 的 CDN URL
    char cf_color[16];      // 当前等级分对应色值，如 "#0000FF"（蓝名）
} UserInfo;
```

**字段来源**：全部来自 CF API 的 `user.info` 接口。`cf_color` 非 API 直接返回，而是由 `cf_api.c` 在填充时通过 `cf_color(rating)` 函数计算得出。

**字符串字段设计**：全部是**内嵌定长数组**而非指针。这意味着：
- 不需要 strdup/free——数据在结构体内部
- memset 即可整体清零
- 超过长度会被截断（CF 用户名实际不超过 24 字符，256 的头像 URL 也足够）

### 2.2 ProblemResult（单题结果）

```c
typedef struct {
    char   index[4];       // 题号，如 "A"、"B1"、"C"
    double points;         // 该题得分
    int    attempt_count;  // 提交次数（含失败）
    int    best_time;      // 首次通过时间（秒，距比赛开始），未通过则为 -1
} ProblemResult;
```

**评分规则**：
- CF 旧赛制（ICPC 风格）：每题 0 或 1.0（通过为 1.0）
- CF 新赛制（分值风格）：每题有自己的满分值（如 500, 1000, 1500...），取最高一次提交的得分
- 本项目统一用 `points` 字段表示，兼容两种赛制

### 2.3 ContestRecord（单场比赛记录）

```c
#define MAX_PROBLEMS 12

typedef struct {
    int    contest_id;         // CF 比赛 ID
    char   contest_name[256];  // 比赛名称
    time_t start_time;         // 比赛开始时间（Unix 时间戳）
    int    old_rating;         // 赛前等级分
    int    new_rating;         // 赛后等级分
    int    rank;               // 比赛排名
    double points;             // 总分
    int    problem_count;      // 实际题目数量（≤ MAX_PROBLEMS）
    ProblemResult problems[MAX_PROBLEMS];  // 各题详情（静态数组）
    int    upsolved_count;                // 赛后补题数
    char   upsolved[MAX_PROBLEMS][4];      // 补做题号数组（如 "A", "C"）
} ContestRecord;
```

**题目数量为什么是 12**：CF 单场最多 12 题（Div.1+Div.2 合并场），静态数组避免了每场比赛动态分配内存的开销。潜在风险：如果 CF 将来增加题目数，需要改宏重新编译。

**补题匹配逻辑**（在 analyzer.c 中实现）：以 `start_time` 为分界线——时间戳 ≤ start_time 的提交为"赛时"，之后为"赛后"。赛后提交中 verdict 为 "OK" 的计入 `upsolved` 数组。

### 2.4 UserData（用户聚合数据）

```c
#define HISTOGRAM_PERIODS 4    // 全部 / 365天 / 180天 / 30天
#define HISTOGRAM_BUCKETS 28   // 800-3500，步长 100
#define HISTOGRAM_MIN     800
#define HISTOGRAM_STEP    100

typedef struct {
    UserInfo info;                       // 基本信息
    int      contest_count;              // 历史总比赛次数
    int      recent_count_180d;          // 近 180 天比赛次数
    int      recent_max_rating_180d;     // 近 180 天最高等级分
    ContestRecord *records;              // 比赛记录数组（动态分配）
    int      record_count;               // 记录数量
    int      histogram[HISTOGRAM_PERIODS][HISTOGRAM_BUCKETS]; // 难度直方图
} UserData;
```

**records 是唯一动态分配的内存**：由 `cf_get_user_ratings` 用 malloc 分配、`userdata_free` 用 free 释放。其他字段全是栈上定长数据，随着 UserData 结构体本身被创建/销毁而自动管理。

**难度直方图的桶映射**：

```
难度值 → 桶索引 = (难度值 - 800) / 100
 800- 899 → 桶 0    标签 "800-899"
 900- 999 → 桶 1    标签 "900-999"
 ...
3400-3499 → 桶 26   标签 "3400-3499"
3500+     → 桶 27   标签 "3500+"
```

四个时段的统计逻辑在 `analyzer.c` 中：
```c
out->histogram[0][bucket]++;   // 全部：无条件计数
if (days_ago <= 365) out->histogram[1][bucket]++;  // 近一年
if (days_ago <= 180) out->histogram[2][bucket]++;  // 近 180 天
if (days_ago <= 30)  out->histogram[3][bucket]++;  // 近 30 天
```

---

## 三、函数详解

### 3.1 userdata_free

```c
void userdata_free(UserData *ud)
{
    if (!ud) return;
    free(ud->records);
    ud->records = NULL;
    ud->record_count = 0;
}
```

**功能**：释放 UserData 中动态分配的内存。

**实现原理**：

1. NULL 检查：防止对 NULL 指针调用 free
2. 释放 records 指向的堆内存（ContestRecord 数组）
3. 将指针置 NULL、计数归零——防御性编程，防止调用者意外重复 free

**为什么只释放 records**：UserData 中只有 `records` 是 malloc 分配的（由 cf_api.c 中 `cf_get_user_ratings` 分配）。其他字段（UserInfo、histogram、基础类型）都在栈上或 UserData 结构体内部，随结构体本身释放即可。

**调用链**：

```
main.c: userdata_free(&users[i])  ← 每个用户处理完成后
main.c: userdata_free(&ud)        ← 单用户模式处理完成后
analyzer.c: userdata_free(out)    ← analyze_user 失败时清理
```

---

## 四、在项目架构中的位置

```
cf_api.c ──填充──▶ UserInfo     (user.info API)
cf_api.c ──填充──▶ ContestRecord (user.rating API)
analyzer.c ──聚合──▶ UserData   (含 histogram)
main.c ──使用──▶ UserData → export_data_js → data.js
                ↓
         userdata_free()
```

data_model 是被所有上层模块引用的"公共词汇表"——每个 .c 文件都通过 `#include "data_model.h"` 来理解同样的类型。

---

## 五、设计特点

| 设计点 | 说明 |
|--------|------|
| 内嵌定长字符串 | handle[64]、rank_name[32] 等全部是定长数组而非 char*，避免额外的 malloc/free，简化内存管理 |
| 静态题目数组 | problems[12] 和 upsolved[12] 是编译期固定大小，每场比赛无额外分配，但受 CF 单场最大题数限制 |
| 单一动态成员 | 只有 records 需要 malloc/free，释放逻辑集中在 userdata_free，避免内存泄漏 |
| 难度直方图硬编码 | 800-3500 范围、100 步长直接定义为宏，CF 实际题目难度不超出此范围 |
| 时段统计耦合 | histogram 的四时段统计直接嵌入 UserData 类型，而非独立结构——简化数据传递 |
