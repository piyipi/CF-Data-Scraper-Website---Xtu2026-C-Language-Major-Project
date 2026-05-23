# 2026-05-21 实现 HTTP 请求模块（libcurl 封装）

**新增文件**
- `Script/http_client.h` — 响应结构体 HttpResponse 定义 + http_init / http_cleanup / http_get 声明
- `Script/http_client.c` — libcurl 封装实现（GET 请求、write callback 动态累积、30s 超时、HTTP 200 校验）

**技术要点**
- 全局初始化/清理：`curl_global_init(CURL_GLOBAL_ALL)` / `curl_global_cleanup()`
- write callback 使用 realloc 累积响应数据，末尾自动补 `\0`
- User-Agent: `CF-Crawler/1.0`
- 仅 HTTP 200 视作成功，其他状态码返回 -1

---

# 2026-05-22 下载 cJSON 库

**新增文件**
- `Script/cJSON.h` — cJSON 头文件（从 GitHub DaveGamble/cJSON 下载，v1.7.18）
- `Script/cJSON.c` — cJSON 源码

---

# 2026-05-22 实现数据结构与工具模块

**新增文件**
- `Script/data_model.h` — 定义 UserInfo / ProblemResult / ContestRecord / UserData 结构体 + userdata_free 声明
- `Script/data_model.c` — userdata_free 实现（释放 records 动态数组）
- `Script/utils.h` — cf_color / cf_rank_name / days_since / max_i 声明
- `Script/utils.c` — CF 等级分→颜色/头衔映射 + 距今日期计算

**结构体设计**
- `UserInfo`: handle(64) / rating / max_rating / rank_name(32) / avatar_url(256) / cf_color(16)
- `ProblemResult`: index(4) / points / attempt_count / best_time
- `ContestRecord`: contest_id / contest_name(256) / start_time / old_rating / new_rating / rank / points / problems[12] / upsolved[12]
- `UserData`: info + contest_count / recent_count_180d / recent_max_rating_180d / records 动态数组

---

# 2026-05-22 实现 Codeforces API 调用模块

**新增文件**
- `Script/cf_api.h` — cf_get_user_info / cf_get_user_ratings / cf_get_user_status / cf_get_contest_list 声明
- `Script/cf_api.c` — 4 个 API 函数的完整实现

**接口清单**
| 函数 | 调用 API | 返回类型 |
|------|---------|---------|
| cf_get_user_info | user.info | UserInfo 结构体 |
| cf_get_user_ratings | user.rating | ContestRecord[] 动态数组 |
| cf_get_user_status | user.status | 原始 cJSON 对象 |
| cf_get_contest_list | contest.list | 原始 cJSON 对象 |

**实现要点**
- 内部封装 `api_get()` 统一处理：URL 组装 → HTTP GET → JSON 解析 → status==OK 校验
- cf_get_user_ratings 使用 malloc 动态分配 ContestRecord 数组，调用者负责 free
- cf_get_user_status 最多取 200 条提交（from=1&count=200）
- contest.list 过滤 gym=false，仅取正式比赛

---

# 2026-05-22 实现数据分析模块

**新增文件**
- `Script/analyzer.h` — analyze_user / export_data_js / analyze_multiple 声明
- `Script/analyzer.c` — 数据聚合 + 补题匹配 + JS 导出实现

**核心逻辑**
- `analyze_user`: 三步聚合 — ① user.info 基本信息 ② user.rating 比赛历史 ③ user.status 提交记录按 contestId 匹配
- 补题匹配：以 ratingUpdateTimeSeconds 为赛时/赛后分界线，赛后 OK 提交计入补题列表
- 各题得分：赛时提交取最高 points（无 points 时按 OK 给 1.0），首次通过时间 = submissionTime - contestEnd
- 统计项：contestCount / recentCount180d / recentMaxRating180d
- `export_data_js`: 输出 `var CF_DATA = { ... };` 格式，escape 处理 JSON 字符串，比赛记录时间从近到远

---

# 2026-05-22 实现前端可视化页面

**新增/修改文件**
- `Script/template.html` — 完整前端页面模板（697 行）
- `Script/main.c` — 程序入口

**模板 HTML 设计**
- 暗色主题 (#0f0f1a)，玻璃拟态卡片，纯 CSS Grid/Flex 布局
- 用户卡片：头像 + 昵称（CF 等级分色）+ 等级分 + 头衔
- 统计行：当前分 / 最高分 / 总场次 / 近 180 天场次
- ECharts 折线图：Rating 变化曲线，节点按对应等级分色着色，最高/最低分金色标记
- 比赛详情表格：日期 / 赛事名 / 排名 / 分差 / 各题状态（绿=通过/红=失败/灰=未做）/ 补题列表
- 响应式：1200px+ / 768px / 480px 三档
- 优雅空状态处理（无比赛数据时显示提示）

**main.c 流程**
- 命令行参数取 handle → http_init → analyze_user → export_data_js → 复制 template.html→index.html → http_cleanup

---

# 2026-05-22 实现多用户支持（选作）

**修改文件**
- `Script/main.c` — 重构入口，支持单用户 / 多参数 / 文件三种模式

**模式切换逻辑**
| 输入 | 行为 |
|------|------|
| `cf_crawler tourist` | 单用户 → data.js + index.html |
| `cf_crawler user1 user2 user3` | 多用户 → 各用户 {handle}_data.js + {handle}.html + 列表页 index.html |
| `cf_crawler users.txt` | 从文件读取 handles，行为同多用户 |

**列表页功能**
- 暗色主题卡片 + 用户摘要表格（头像 / 昵称 / 等级分 / 最高分 / 头衔 / 总场次 / 近 180 天）
- 统计卡片行：用户数 / 总场次 / 全场最高分
- 表头点击排序（数字列数值排序，文本列字典序）
- 点击行跳转个人详情页 `{handle}.html`

---

# 2026-05-22 实现难度直方图（选作）

**修改文件**

- `Script/data_model.h` — UserData 新增 `histogram[4][28]` 字段 + 直方图常量定义
- `Script/analyzer.c` — analyze_user 内遍历所有 OK 提交的 problem.rating，按 4 个时段分桶统计；export_data_js 输出 histogram JSON
- `Script/template.html` — 新增直方图 ECharts 柱状图 + 4 时段 tab 切换

**直方图设计**
- X 轴：难度区间（800-899, 900-999, …, 3400-3499），共 28 桶
- Y 轴：通过题目数量
- 4 个时段：全部 / 近 365 天 / 近 180 天 / 近 30 天，tab 切换
- 柱子颜色按对应难度等级分色着色
- 窗口 resize 自动重绘

---

# 2026-05-23 编译测试与设计文档

## 环境配置

- 确认编译器：MinGW-w64 GCC 8.1.0（`D:\VSCode\VSCplug-in\mingw64\`）
- 下载 libcurl 8.20.0 开发包（curl-for-win 构建，含静态库 + 头文件 + DLL）
- 复制 libcurl 头文件/库/DLL 到 MinGW 目录

## 编译验证

- 修复 `_strdup` → `strdup`（MSVC-ism 修正）
- 修复 `analyze_multiple` 类型不匹配警告（`UserData **` → `UserData *`）
- 动态链接编译成功：`cf_crawler.exe`（125 KB），零错误，仅一个未使用函数警告

## 功能测试

| 模式 | 命令 | 结果 |
|------|------|------|
| 单用户 | `cf_crawler tourist` | ✅ data.js (55KB) + index.html 正常生成 |
| 多用户 | `cf_crawler tourist Petr` | ✅ 各自页面 + 列表页正常生成 |
| 文件输入 | `cf_crawler users.txt` | ✅ 读取文件 handles 正常 |
| 错误处理 | `cf_crawler InvalidUser` | ✅ HTTP 400 正确捕获并报错 |

## 编译脚本

- 创建 `Script/Makefile`：支持 `all` / `test` / `clean` / `dist` 目标
- 创建 `users.txt` 示例文件

## 设计文档

- 编写 `设计文档.md`：涵盖项目概述 / 架构 / 模块设计 / 数据结构 / API 集成 / 前端设计 / 编译说明 / 使用说明 / 测试结果 / 目录结构

---
