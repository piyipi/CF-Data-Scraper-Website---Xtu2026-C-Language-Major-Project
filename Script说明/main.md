# main —— 程序入口与调度层

**文件**：`src/main.c`（454 行）

**角色**：程序的唯一入口。负责命令行解析、模式分派、流程编排，以及多用户列表页的 HTML 生成。核心处理逻辑封装在 `process_handles()` 中，由 `main()` 和 `interactive_mode()` 共享。每生成一个文件，向 stdout 打印 `Generated <path>`。

---

## 一、四种运行模式

| 模式 | 命令/输入示例 | 触发条件 | 输出 |
|------|-------------|---------|------|
| 交互模式 | 双击 exe（无命令行参数） | `argc < 2` | 提示输入后按单/多/文件模式处理 |
| 单用户 | `cf_crawler tourist` | `argc == 2` 且不以 `.txt` 结尾 | `output/data.js` + `output/index.html` |
| 多用户 | `cf_crawler tourist Petr Benq` | `argc >= 3` | 各自 `{handle}_data.js` + `{handle}.html` + 列表页 |
| 文件模式 | `cf_crawler users.txt` | `argc == 2` 且以 `.txt` 结尾 | 同多用户模式 |

**交互模式增强**（2026-05-31）：交互模式不再局限于单用户——输入框支持三种格式：
- 单个 handle → 单用户处理
- 空格分隔的多个 handle → 多用户处理（含列表页）
- `.txt` 文件路径 → 从文件批量读取

```c
if (argc < 2) {
    http_init();
    int ret = interactive_mode();   // 进入交互模式
    http_cleanup();
    return ret;
}
```

**命令行模式判断逻辑**（`main()` 中）：

```c
if (argc == 2 && str_ends_with(argv[1], ".txt")) {
    read_handles_file(argv[1], &handles, &hcount);
    from_file = 1;
} else if (argc >= 3) {
    hcount  = argc - 1;
    handles = &argv[1];
} else {
    hcount  = 1;
    handles = &argv[1];
}
int ret = process_handles(handles, hcount);  // 共享处理逻辑
```

---

## 二、内部函数详解

### 2.0 process_handles —— 共享处理逻辑（核心）

```c
static int process_handles(char **handles, int hcount)
```

**角色**：分析一组 handles 并生成全部输出文件。由 `main()` 和 `interactive_mode()` 共用，消除代码重复。

**流程**：

1. `calloc(hcount, sizeof(UserData))` 分配用户数组
2. 循环 `analyze_user()` 分析每个用户
3. 全失败检查 → 返回 -1
4. 单用户 → `output/data.js` + `output/index.html`
5. 多用户 → 逐用户 `{handle}_data.js` + `{handle}.html` + `generate_list_page()` 列表页
6. `userdata_free()` × hcount + `free(users)` 清理
7. 返回 0（成功）或 -1（失败）

**调用方职责**：

- `main()`：调用前需 `http_init()`，调用后需 `http_cleanup()` + 释放 `from_file` 场景的 handles
- `interactive_mode()`：自行解析输入为 handles 数组，调用前后不需要 http_init/cleanup（由 main 的交互分支负责）

### 2.1 interactive_mode —— 交互模式

```c
static int interactive_mode(void)
```

**功能**：程序无参数启动时进入的交互式界面。提示用户输入，根据输入内容自动判断模式：

| 输入格式 | 判断逻辑 | 处理方式 |
|---------|---------|---------|
| `.txt` 结尾 | `str_ends_with(input, ".txt")` | 调用 `read_handles_file()` 解析文件 → `process_handles()` |
| 其他 | `strtok(input, " ")` 按空格分词 | 动态分配 handles 数组 → `process_handles()` |

输入示例：`tourist`（单用户）、`tourist Petr Benq`（多用户）、`users.txt`（文件模式）。

**资源管理**：handles 数组在交互模式中始终由 `strdup` / `read_handles_file` 在堆上分配，`process_handles()` 返回后统一 `free`。

### 2.2 str_ends_with —— 文件名后缀判断

```c
static int str_ends_with(const char *s, const char *suffix)
```

**功能**：判断字符串 `s` 是否以 `suffix` 结尾。

**在项目中的用途**：识别 `.txt` 文件模式——交互模式和命令行参数都需要判断输入是否为文件路径。

**实现**：

```c
size_t sl = strlen(s), sufl = strlen(suffix);
if (sufl > sl) return 0;                        // 后缀比原串长 → 不可能匹配
return strcmp(s + sl - sufl, suffix) == 0;      // 指针偏移到末尾等长切片, strcmp 比较
```

**为什么先比较长度**：`size_t` 是无符号类型，若 `sufl > sl` 直接做 `sl - sufl` 会回绕为极大的正数，`s + 极大值` 指向非法内存。前置守卫 `if (sufl > sl) return 0` 消除此风险。

**返回值**：
- 1：`s` 以 `suffix` 结尾
- 0：不匹配（后缀过长 或 内容不同）

### 2.3 read_handles_file —— 读取用户列表文件

```c
static int read_handles_file(const char *path, char ***handles_out, int *count_out)
```

**功能**：从文本文件中逐行读取 CF handles，支持注释和空行。

**处理规则**：

```
文件内容：                   解析结果：
# 我的关注列表              → 跳过（# 开头）
tourist                     → "tourist"
 Petr                       → "Petr"（去除首尾空格）
                            → 跳过（空行）
# KAN                       → 跳过（# 开头）
Benq                        → "Benq"
```

**实现细节**：
```c
while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = '\0';   // 去除 Windows/Linux 换行符
    if (line[0] == '\0' || line[0] == '#') continue;  // 跳过空行和注释

    // 去除首尾空格
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s) - 1;
    while (e > s && (*e == ' ' || *e == '\t')) *e-- = '\0';

    handles[cnt] = strdup(s);    // 复制到新分配的内存
    cnt++;
}
```

**内存管理**：
- `handles` 数组本身由 malloc 分配
- 每个 handle 字符串由 strdup 分配
- 文件模式结束时由 main 函数负责 free

### 2.4 generate_list_page —— 多用户列表页生成

```c
static int generate_list_page(UserData *users, int count, const char *filepath)
```

**功能**：直接通过 `fprintf` 生成一个完整的 HTML 文件，展示多用户摘要表格。

**页面结构**：

```
┌──────────────────────────────────────────┐
│          Codeforces Rating Viewer        │
│          Multi-User Summary              │
├──────────────────────────────────────────┤
│  Users: N   Total Contests: M   Max: XXXX│  ← 统计卡片
├──────────────────────────────────────────┤
│ # │ Handle │ Rating │ Max │ Rank │ ...  │  ← 可排序表格
├───┼────────┼────────┼─────┼──────┼──────│
│ 1 │ tourist│  3774  │ 4009│  LGM │ ...  │
│ 2 │ Petr   │  3100  │ 3200│  GM  │ ...  │
│ 3 │ Benq   │  3500  │ 3800│  LGM │ ...  │
└───┴────────┴────────┴─────┴──────┴──────┘
```

**为什么用 fprintf 硬编码 HTML**：列表页结构固定、逻辑简单，直接用 fprintf 输出比"读取模板 + 文本替换"更直接。CSS 内嵌在 `<style>` 标签中，无需外部样式表。

**排序功能**：表格表头可点击排序（纯前端 JS 实现）：

```javascript
function sortTable(col) {
    // 获取所有行 → 按第 col 列排序 → 重新插入 tbody
    var asc = 判断升序/降序;
    rows.sort(function(a, b) {
        var na = parseFloat(a.cells[col]);
        // 如果可解析为数字 → 按数值排序；否则 → 按字符串排序
        return asc ? na - nb : nb - na;
    });
}
```

**统计卡片**：计算三个汇总指标——用户总数、总比赛场次、全场最高 Max Rating。

---

## 三、main 函数完整执行流程

```
main(argc, argv)
 │
 ├─ ① argc < 2 → 交互模式（interactive_mode）
 │     ├─ 提示输入 → 解析为 handles 数组
 │     │   （单handle / 空格分词 / .txt文件 → read_handles_file）
 │     ├─ process_handles(handles, hcount)  ← 共享处理逻辑
 │     └─ 释放 handles → 按 Enter 退出
 │
 ├─ ② http_init()
 │
 ├─ ③ 模式判断 → 构造 handles 数组
 │     ├─ .txt 文件 → read_handles_file() (from_file=1)
 │     ├─ argc≥3   → handles = &argv[1] (指向 argv)
 │     └─ argc==2  → handles = &argv[1] (hcount=1)
 │
 ├─ ④ process_handles(handles, hcount)  ← 共享处理逻辑
 │     ├─ calloc(hcount) → 循环 analyze_user → 全失败检查
 │     ├─ 单用户 → data.js + index.html
 │     └─ 多用户 → 逐用户页 + 列表页
 │
 ├─ ⑤ from_file 清理：free(handles[i]) + free(handles)
 ├─ ⑥ http_cleanup()
 └─ ⑦ return
```

**为什么不是文本替换**：`web/template.html` 通过动态脚本根据 HTML 文件名推导对应数据文件名（`index.html` → `data.js`，`{handle}.html` → `{handle}_data.js`），因此无需在复制时修改模板内容——复制后直接可用。

### template.html 复制机制

```c
FILE *src = fopen("web/template.html", "r");    // 第一优先级：项目 web/ 目录
if (!src) src = fopen("template.html", "r");    // 回退：当前目录
if (src) {
    FILE *dst = fopen(html_path, "w");
    // 4KB 缓冲区逐块复制
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        fwrite(buf, 1, n, dst);
    fclose(dst);
    printf("  Generated %s\n", html_path);      // 确认 HTML 输出位置
} else {
    fprintf(stderr, "Warning: template.html not found\n");
}
fclose(src);
```

### 文件输出确认

每生成一个文件，向 stdout 打印 `Generated <relative_path>`，覆盖全部输出路径：
- JS 数据文件：`export_data_js` 成功后立即打印
- HTML 页面：模板复制成功后打印
- 多用户列表页：`generate_list_page` 成功后打印

这使得用户在命令行中能清晰看到所有输出文件的位置，无需事后查找。

---

## 四、输出文件命名规则

| 模式 | data.js | index.html | 额外输出 |
|------|---------|------------|---------|
| 单用户 `tourist` | `output/data.js` | `output/index.html` | — |
| 多用户 `tourist Petr` | `output/tourist_data.js` | `output/index.html`（列表页） | `output/tourist.html`、`output/Petr_data.js`、`output/Petr.html` |
| 文件模式 | 同多用户 | 同多用户 | 同多用户 |
| 交互模式 | `output/{handle}_data.js` | `output/{handle}.html` | — |

**注意**：所有输出文件统一写入 `output/` 子目录。C 代码不会自动创建 `output/` 目录——如果目录不存在，`fopen` 会失败（程序报错但不会崩溃）。Makefile 的 `test` 目标已包含 `mkdir output` 以确保目录存在。

---

## 五、错误处理策略

| 错误场景 | 处理方式 |
|---------|---------|
| 无效参数 | usage() 打印帮助信息，return 1 |
| 文件不存在（users.txt） | fprintf + return 1 |
| 单个用户拉取失败 | fprintf + 跳过该用户，继续处理下一个 |
| 所有用户全部失败 | fprintf + 清理资源 + return 1 |
| template.html 找不到 | fprintf 警告 + 跳过 HTML 生成（不影响 data.js 输出） |
| 内存不足（calloc 失败） | fprintf + 清理 + return 1 |

**关键设计哲学**：单个用户失败不中断整体流程（多用户模式下），但全部失败时退出。template.html 缺失时只生成 data.js 不生成 HTML——属于降级而非崩溃。

---

## 六、在项目架构中的位置

```
main.c
 ├─ http_init() / http_cleanup()       ← libcurl 生命周期管理
 ├─ analyze_user() × N                 ← 委托 analyzer 完成分析
 ├─ export_data_js() × N               ← 导出 JS 数据文件
 ├─ template.html 复制 × N             ← 生成 HTML 页面
 └─ generate_list_page()              ← 多用户列表页
```

main.c 是分层架构的顶层——它不依赖任何上层模块（因为没有），只调用下层提供的接口。所有业务逻辑通过 `analyze_user` 委托给 analyzer 层。

---

## 七、设计特点

| 设计点 | 说明 |
|--------|------|
| 薄入口 | main 只做调度和编排，无业务逻辑——所有分析在 analyzer 中 |
| 数据模板分离 | 页面 = 模板（template.html） + 数据（{handle}_data.js），修改样式只需改模板 |
| 容错降级 | 模板缺失、单用户失败均不崩溃，输出警告后继续 |
| 动态 allocate（文件模式）+ 静态引用（命令行模式） | 文件模式下 handles 需要 malloc+strdup（因为 fgets 的 buffer 是局部的），命令行模式下直接指针指向 argv——两种模式在后续循环中被统一处理 |
