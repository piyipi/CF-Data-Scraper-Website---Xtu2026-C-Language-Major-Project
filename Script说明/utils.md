# utils —— 工具函数层

**文件**：`src/utils.h`（20 行）+ `src/utils.c`（38 行）

**角色**：提供三个全局工具函数和一个内联宏。代码量最小，但被几乎所有上层模块引用。

---

## 一、对外接口

| 函数 | 功能 | 调用方 |
|------|------|--------|
| `cf_color(rating)` | 等级分 → 颜色色值 | `cf_api.c`、`main.c` |
| `cf_rank_name(rating)` | 等级分 → 头衔名称 | 定义但未被调用（cf_api 直接用 API 返回的 rank 字段） |
| `days_since(t)` | 计算给定时间戳距今多少天 | `analyzer.c` |
| `max_i(a, b)` | 两整数取大（内联宏） | 定义但未被调用 |

---

## 二、函数实现原理

### 2.1 cf_color —— 等级分颜色映射

```c
const char *cf_color(int rating)
{
    if (rating <  1200) return "#808080";  /* Newbie       灰 */
    if (rating <  1400) return "#008000";  /* Pupil        绿 */
    if (rating <  1600) return "#03A89E";  /* Specialist   青 */
    if (rating <  1900) return "#0000FF";  /* Expert       蓝 */
    if (rating <  2100) return "#AA00AA";  /* Candidate Master 紫 */
    if (rating <  2400) return "#FF8C00";  /* Master / IM  橙 */
    if (rating <  3000) return "#FF0000";  /* Grandmaster / IGM 红 */
    return               "#CC0000";        /* Legendary Grandmaster 暗红 */
}
```

**实现原理**：纯 if-else 阶梯查找，O(1) 时间复杂度（最多 7 次比较）。使用 `const char *` 返回值直接指向静态字符串字面量，无需 malloc，无需调用者 free。

**CF 官方颜色与等级分对照**：

| 等级分范围 | 颜色 | 色值 | 头衔 |
|-----------|------|------|------|
| < 1200 | 灰 | #808080 | Newbie |
| 1200–1399 | 绿 | #008000 | Pupil |
| 1400–1599 | 青 | #03A89E | Specialist |
| 1600–1899 | 蓝 | #0000FF | Expert |
| 1900–2099 | 紫 | #AA00AA | Candidate Master |
| 2100–2399 | 橙 | #FF8C00 | Master / International Master |
| 2400–2999 | 红 | #FF0000 | Grandmaster / International Grandmaster |
| ≥ 3000 | 暗红 | #CC0000 | Legendary Grandmaster |

**注意**：2100-2399 跨越了 Master 和 International Master 两个头衔，但颜色相同（橙色），因此颜色函数可以合并。3000+ 使用暗红而非正红，与 CF 官网一致。

**调用路径**：

```
cf_api.c: cf_get_user_info()
        → snprintf(info->cf_color, ..., "%s", cf_color(info->rating))
        将颜色字符串存入 UserInfo.cf_color

main.c: generate_list_page()
        → cf_color(max_overall)    获取最高分的颜色用于统计卡片
        → cf_color(u->info.max_rating)  获取每位用户最高分的颜色用于表格
```

### 2.2 cf_rank_name —— 等级分头衔名称

```c
const char *cf_rank_name(int rating)
{
    if (rating <  1200) return "Newbie";
    if (rating <  1400) return "Pupil";
    if (rating <  1600) return "Specialist";
    if (rating <  1900) return "Expert";
    if (rating <  2100) return "Candidate Master";
    if (rating <  2300) return "Master";
    if (rating <  2400) return "International Master";
    if (rating <  2600) return "Grandmaster";
    if (rating <  3000) return "International Grandmaster";
    return               "Legendary Grandmaster";
}
```

**实现原理**：与 cf_color 相同的 if-else 阶梯结构，但分档更细（颜色函数有 7 档，头衔函数有 10 档）。这是因为多个头衔共享同一种颜色（如 Grandmaster 和 International Grandmaster 都是红色，但头衔不同）。

**实际使用情况**：定义但**未被任何模块调用**。cf_api.c 直接从 CF API 的 `user.info` 接口获取 `rank` 字段填入 `UserInfo.rank_name`，不通过此函数计算。该函数作为备用/参考保留。

### 2.3 days_since —— 日期差计算

```c
int days_since(time_t t)
{
    time_t now = time(NULL);
    double diff = difftime(now, t);
    return (int)(diff / 86400.0);
}
```

**实现原理**：
1. `time(NULL)` 获取当前 Unix 时间戳（秒）
2. `difftime(now, t)` 计算两个时间戳的差值（double 类型，精确到秒）
3. 除以 86400（一天 = 24 × 60 × 60 秒）得到天数
4. 强制转换 `(int)` 截断小数部分，得到整数天数

**为什么用 difftime 而非直接相减**：C 标准规定 `time_t` 不保证是算术类型（在某些系统上可能是结构体），`difftime` 是标准规定的可移植差值计算方法。虽然 MinGW-w64 上 `time_t` 实际就是 `long long`，直接用减法也能工作，但使用 difftime 保证可移植性。

**调用路径**：

```c
// analyzer.c 中两次使用：
if (days_since(rec->start_time) <= 180) {
    out->recent_count_180d++;             // 近 180 天比赛计数
    if (rec->new_rating > out->recent_max_rating_180d)
        out->recent_max_rating_180d = rec->new_rating;  // 更新近 180 天最高分
}

// 难度直方图统计中：
double days_ago = difftime(now, (time_t)sub_time) / 86400.0;
if (days_ago <= 365) out->histogram[1][bucket]++;  // 注意：这里直接用 difftime 而非 days_since
```

**注**：难度直方图统计中直接用了 `difftime() / 86400.0`，没有调用 `days_since()`。原因可能是为了避免在循环内部重复调用 `time(NULL)`（days_since 每次都会获取当前时间），而是预先获取一次 `now` 并复用。这是一个微优化——在内层循环（最多 200 条提交 × 每条多次比较）中避免不必要的系统调用。

---

## 三、在项目架构中的位置

```
cf_api.c   ──调用──▶ cf_color()       ← 填充 UserInfo.cf_color
analyzer.c ──调用──▶ days_since()     ← 近 180 天筛选
main.c     ──调用──▶ cf_color()       ← 多用户列表页着色
cf_api.h   ──引用──▶ utils.h          ← 间接获得 cf_color 声明
```

---

## 四、设计特点

| 设计点 | 说明 |
|--------|------|
| 静态字符串返回 | cf_color 和 cf_rank_name 返回 const char* 指向字面量，无需 malloc/free，零内存开销 |
| 纯函数 | 所有工具函数无副作用、无全局状态、无 I/O，输入确定则输出确定 |
| 极简接口 | 仅 3 个函数 + 1 个内联宏，58 行代码，职责清晰 |
| 头文件内联宏 | `max_i` 用 `static inline` 在头文件中定义，编译期展开，无函数调用开销（虽然实际未被使用） |
