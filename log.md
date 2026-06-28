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

# 2026-05-31 项目结构重组、API 请求修复、交互模式与文档更新

## 一、修复 API 请求失败问题

**问题**：CF API 偶发 503 / 网络超时，程序直接报错退出无重试；连续快速请求触发 429 限流。

**修改文件**：
- `src/http_client.c` — 新增重试与限流处理
- `src/analyzer.c` — 请求间隔节流

### 1.1 自动重试机制（http_client.c）

`http_get()` 新增最多 **3 次** 重试循环：

```c
for (int attempt = 0; attempt < 3; attempt++) {
    CURL *curl = curl_easy_init();
    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (status == 200) return 0;   // 成功

        if (status == 429) {            // 限流 → 等 2s 再试
            Sleep(2000);
            continue;
        }
        // 其他 HTTP 错误 → 等 1s 再试
        Sleep(1000);
    } else {
        // curl 错误 → 等 1s 再试
        Sleep(1000);
    }
}
// 3 次全部失败 → 返回 -1
```

### 1.2 HTTP 429 限流处理

当 CF API 返回 HTTP 429 (Too Many Requests) 时：
- 打印警告日志 `HTTP 429 rate-limited (attempt N/3)`
- 等待 **2 秒** 后重试（比普通错误等更久，给服务端恢复时间）

### 1.3 请求间隔节流（analyzer.c）

`analyze_user()` 中每次 API 调用之间插入 **1.5 秒** 延迟：

```c
cf_get_user_info(handle, &out->info);
Sleep(1500);  /* 避免 CF 限流 */

cf_get_user_ratings(handle, &out->records, &out->record_count);
Sleep(1500);  /* 避免 CF 限流 */

cf_get_user_status(handle, &status_root);
```

### 1.4 可重入初始化（http_client.c）

`http_init()` / `http_cleanup()` 增加静态状态标记，防止重复 `curl_global_init`。

---

## 二、目录结构重组

**背景**：原代码位于 `Script/` 目录，模板、编译产物、文档混放。为规范化项目结构，将源码、静态资源、输出目录分离。

**变更内容**：

| 旧路径 | 新路径 | 说明 |
|--------|--------|------|
| `Script/main.c` 等 | `src/main.c` 等 | C 源码统一迁移至 `src/` |
| `Script/template.html` | `web/template.html` | 前端模板归入 `web/` |
| `Script/cJSON.*` | `lib/cJSON/cJSON.*` | 第三方库归入 `lib/` |
| `Script/libcurl-x64.dll` | `lib/curl/libcurl-x64.dll` | libcurl 运行时依赖归入 `lib/curl/` |
| `Script/Makefile` | `Makefile`（根目录） | 编译脚本提升至项目根，源码路径更新为 `src/` |
| （无） | `output/` | 新增输出目录，所有生成文件统一写入此目录 |
| `Script/cf_crawler.exe` | `cf_crawler.exe`（根目录） | 编译产物位于项目根 |

**Makefile 更新**：
- `SRCDIR` 从 `Script` 改为 `src`
- `INCLUDES` 增加 `-Isrc`
- 新增 `copy libcurl-x64.dll → lib/curl/` 步骤
- `dist` 目标从 `web/template.html` 复制模板

---

## 三、新增交互模式

**修改文件**：`src/main.c`

当程序不带任何参数运行时（双击 exe 或直接 `.\cf_crawler.exe`），不再打印 usage 退出，而是进入**交互模式**：

```c
static int interactive_mode(void) {
    char handle[64];
    printf("Enter Codeforces handle: ");
    fgets(handle, sizeof(handle), stdin);
    // → analyze_user → export_data_js → 复制 template.html → "Press Enter to exit"
}
```

**行为**：
- 提示用户输入 CF 用户名
- 拉取数据后生成 `output/{handle}_data.js` + `output/{handle}.html`
- 打印摘要后等待用户按 Enter 退出

**设计目的**：方便非技术用户直接双击 exe 使用，无需打开命令行。

---

## 四、使用文档更新

更新 `使用文档.md` 以反映上述所有变更：

- 运行模式从 3 种扩展为 **4 种**（新增交互模式）
- 输出路径统一说明为 `output/` 子目录
- 文件结构图更新为当前 `src/` `web/` `lib/` `output/` 四层布局
- 编译路径从 `cd Script && mingw32-make` 改为根目录直接 `mingw32-make`

---

# 2026-05-31 修复模板数据文件路径不匹配问题

## 问题描述

多用户模式 / 交互模式下，前端页面无法加载数据。浏览器控制台报 `CF_DATA not defined. data.js may not have loaded.`

## 根因

`web/template.html` 中硬编码了 `<script src="data.js">`，但 C 代码在非单用户模式下生成的数据文件名为 `{handle}_data.js`，HTML 文件名为 `{handle}.html`。浏览器按相对路径解析 `data.js` → 查找 `output/data.js`（不存在）→ 加载失败。

| 模式 | 数据文件 | HTML 期望加载 | 实际存在? |
|------|---------|-------------|----------|
| 单用户 | `output/data.js` | `data.js` | ✅ |
| 多用户 | `output/jiangly_data.js` | `data.js` | ❌ 不存在 |
| 交互 | `output/jiangly_data.js` | `data.js` | ❌ 不存在 |

## 修复方案

**修改文件**：`web/template.html`（第 421 行）

**改前**：
```html
<script src="data.js"></script>
```

**改后**：
```html
<script>
  // 根据 HTML 文件名动态推导对应数据文件名：
  //  index.html  →  data.js (单用户模式)
  //  {handle}.html → {handle}_data.js (多用户/交互模式)
  (function(){var n=window.location.pathname.split('/').pop().replace('.html','');
  var js=n==='index'||n===''?'data.js':n+'_data.js';
  document.write('<script src="'+js+'"><\/script>');})();
</script>
```

**原理**：
1. 通过 `window.location.pathname` 获取当前 HTML 文件路径
2. 提取文件名，去掉 `.html` 后缀得到 base 名
3. 若 base 为 `index` → 加载 `data.js`（单用户模式兼容）
4. 否则 → 加载 `{base}_data.js`
5. 使用 `document.write` 在 HTML 解析阶段同步插入 `<script>` 标签，确保后续脚本块中的 `CF_DATA` 正常可用

**文档更新**：
- `Script说明/template.md` — 更新第 4.1 节「数据加载」和第 5 节「与 C 程序的数据接口」

**影响范围**：仅修改一个前端文件，C 代码无需改动

---

# 2026-05-31 新增文件输出路径日志

## 改动背景

此前程序生成 JS 数据文件和 HTML 页面时，部分模式缺少 `Generated <path>` 输出，用户需要手动猜测或查找输出文件位置。

## 修改内容

**修改文件**：`src/main.c`

### 新增 `printf("  Generated %s\n", path)` 的位置：

| 位置 | 模式 | 输出 |
|------|------|------|
| `main.c:143` | `process_single()` JS | `Generated output/{handle}_data.js` |
| `main.c:159` | `process_single()` HTML | `Generated output/{handle}.html` |
| `main.c:408` | `main()` 多用户循环内 JS | `Generated output/{handle}_data.js` |
| `main.c:420` | `main()` 多用户循环内 HTML | `Generated output/{handle}.html` |

### 已有输出保持不变：

| 位置 | 模式 | 输出 |
|------|------|------|
| `main.c:48` | `interactive_mode()` JS | `Generated output/{handle}_data.js` |
| `main.c:62` | `interactive_mode()` HTML | `Generated output/{handle}.html` |
| `main.c:382` | `main()` 单用户 JS | `Generated output/data.js` |
| `main.c:395` | `main()` 单用户 HTML | `Generated output/index.html` |
| `main.c:428` | `main()` 多用户列表页 | `Generated output/index.html (multi-user list)` |

## 最终效果

```
Processing 3 user(s)...
[1/3] tourist ...
  Generated output/tourist_data.js
  Generated output/tourist.html
[2/3] Petr ...
  Generated output/Petr_data.js
  Generated output/Petr.html
[3/3] jiangly ...
  Generated output/jiangly_data.js
  Generated output/jiangly.html

Generating list page...
  Generated output/index.html (multi-user list)

Done! 3/3 users processed. Open output/index.html in your browser.
```

**所有 4 种运行模式**（交互 / 单用户 / 多用户 / 文件）现在均在每生成一个文件时立即打印其相对路径。

## 文档更新

- `Script说明/main.md` — 更新行数（381→439），补充执行流程图中的路径输出步骤，新增「文件输出确认」小节

---

# 2026-05-31 交互模式增强 + process_handles 重构

## 一、交互模式支持多用户 / 文件输入

**背景**：交互模式仅支持输入单个 handle，无法批量处理。

**修改文件**：`src/main.c` — `interactive_mode()` 函数重写

**新的交互界面**：
```
Codeforces Crawler — Interactive Mode
  Single user : tourist
  Multi user  : tourist Petr Benq
  From file   : users.txt

Enter handle(s) or file:
```

**输入解析逻辑**：
| 输入 | 判断 | 行为 |
|------|------|------|
| `.txt` 结尾 | `str_ends_with(input, ".txt")` | `read_handles_file()` 解析 → 批量处理 |
| 空格分隔多个词 | `strtok(input, " ")` | 分词为 handles 数组 → 多用户/单用户处理 |
| 单个词 | 无空格 / 无 .txt | 单 handle → 单用户处理 |

**内存管理**：交互模式中 handles 均由 `strdup` / `read_handles_file` 在堆上分配，`process_handles()` 返回后统一 `free`。

## 二、提取 process_handles() 共享函数

**背景**：`main()` 和 `interactive_mode()` 中存在大量重复的「分析 + 输出」逻辑。

**修改文件**：`src/main.c`

新增 `static int process_handles(char **handles, int hcount)`，将以下逻辑封装为可复用函数：
1. `calloc` 分配 UserData 数组
2. 循环 `analyze_user()` 分析每个用户
3. 全失败检查
4. 单用户 → `data.js` + `index.html`
5. 多用户 → 逐用户页 + `generate_list_page()` 列表页
6. `userdata_free` + `free(users)` 清理

**调用方**：
- `main()` → 模式判断后调用，之后只需 from_file 清理 + http_cleanup
- `interactive_mode()` → 解析输入后调用，之后只需 free handles

**代码量**：`main.c` 从 550 行缩减至 454 行（减少 ~100 行重复代码）。

## 三、文档更新

- `Script说明/main.md` — 更新行数；运行模式从三种扩展为四种；新增 2.0 process_handles + 2.1 interactive_mode 文档；更新执行流程图
- `Script说明/interactive_mode.md` — 待后续补充

---

# 2026-05-31 user.status 分页拉取 — 突破 1000 条提交限制

## 问题背景

`cf_get_user_status()` 原有实现为单次 API 调用 `from=1&count=1000`，CF API 单次最多返回 1000 条。对于提交量大于 1000 的用户（如 tourist 有 5000+ 条），旧比赛的补题数据和难度直方图统计都会丢失。

## 修改文件

| 文件 | 改动 |
|------|------|
| `src/cf_api.c` | `cf_get_user_status()` 重写为分页循环（+70 行） |
| `src/cf_api.h` | 更新函数注释 |
| `设计文档.md` | 更新 API 参数说明 |
| `使用文档.md` | 更新限制说明 |

## 核心实现（cf_api.c）

**函数不变** — 签名 `int cf_get_user_status(const char *handle, cJSON **root)` 保持不变，`analyzer.c` 零改动。

**分页逻辑**：
1. 第 1 页 `from=1, count=1000` — 正常获取，取出 `result` 数组
2. 若本页满 1000 条且 `from + 1000 - 1 <= 10000`（CF 硬上限），继续拉下一页
3. 后续页 `from=1001/2001/…，count=1000` — `Sleep(1000)` 限流
4. 逐条 `cJSON_DetachItemFromArray` → `cJSON_AddItemToArray` 合并入首页 `result` 数组
5. 停止条件：本页不满 1000 条（数据耗尽） / 超 10000 上限 / API 错误

**容错设计**：
- 中间页 API 失败 → `break` 保留已有数据，不丢已拉取的提交
- 新增 `#include <windows.h>` 用于 `Sleep()`

## 效果验证

| 指标 | 改动前 | 改动后 |
|------|--------|--------|
| 最大提交数 | 1,000 | 10,000（CF API 硬上限） |
| 单次 API 调用数 | 1 | 1–10（自动） |
| tourist data.js 大小 | 55 KB | 145 KB |
| `analyzer.c` 改动 | — | 零改动 |

编译 0 error, 0 warning。tourist 测试中偶发分页 API 超时由 `http_client` 的 3 次重试机制优雅处理，已有数据不受影响。
