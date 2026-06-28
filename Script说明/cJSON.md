# cJSON —— JSON 解析库（第三方）

**文件**：`lib/cJSON/cJSON.h`（306 行）+ `lib/cJSON/cJSON.c`（3206 行）

**来源**：DaveGamble/cJSON v1.7.18（GitHub）

**角色**：项目的数据基石。Codeforces API 返回 JSON 格式数据，C 语言没有原生 JSON 解析能力，因此引入 cJSON 承担所有 JSON ↔ C 结构体的转换工作。

---

## 一、核心数据结构

### cJSON 节点

```c
typedef struct cJSON {
    struct cJSON *next;       // 同级下一节点（数组/对象的链表遍历）
    struct cJSON *prev;       // 同级上一节点
    struct cJSON *child;      // 子节点链（数组/对象的第一个子元素）

    int type;                 // 节点类型（位掩码）

    char *valuestring;        // 字符串值（type 包含 cJSON_String 或 cJSON_Raw 时有效）
    int valueint;             // 整数值（已弃用，推荐用 cJSON_SetNumberValue）
    double valuedouble;       // 浮点数值（type 包含 cJSON_Number 时有效）

    char *string;             // 键名（当该节点是对象的成员时）
} cJSON;
```

### 节点类型

```
cJSON_Invalid (0)        无效节点
cJSON_False   (1 << 0)   布尔假
cJSON_True    (1 << 1)   布尔真
cJSON_NULL    (1 << 2)   空值
cJSON_Number  (1 << 3)   数字
cJSON_String  (1 << 4)   字符串
cJSON_Array   (1 << 5)   数组
cJSON_Object  (1 << 6)   对象
cJSON_Raw     (1 << 7)   原始 JSON（不经转义直接输出）
```

额外的标志位（可与上述类型组合）：
```
cJSON_IsReference    256   标记为引用（cJSON_Delete 不释放子节点/字符串）
cJSON_StringIsConst  512   标记字符串为常量（cJSON_Delete 不释放 string 字段）
```

### JSON 输入到 C 结构体的映射

```
JSON:                                  cJSON 树:
{                                      root (type=Object)
  "status": "OK",          →           ├─ child "status" (type=String, valuestring="OK")
  "result": [                          └─ next  "result" (type=Array)
    {"handle": "tourist", ...},  →              ├─ child[0] (type=Object, child="handle"→...)
    {"handle": "Petr", ...}     →              └─ next[1] (type=Object, child="handle"→...)
  ]
}
```

---

## 二、本项目实际使用的 API

| API | 作用 | 调用方 |
|-----|------|--------|
| `cJSON_Parse(str)` | 将 JSON 字符串解析为 cJSON 树 | `cf_api.c` → 每个 API 响应回来第一步 |
| `cJSON_GetObjectItem(obj, key)` | 从 JSON 对象中按键名取值（大小写不敏感） | `cf_api.c` → 提取 handle/rating/avatar 等字段 |
| `cJSON_GetArrayItem(arr, i)` | 从 JSON 数组中按索引取值 | `cf_api.c` → 遍历比赛列表/提交列表 |
| `cJSON_IsString(obj)` / `cJSON_IsNumber(obj)` / `cJSON_IsArray(obj)` | 类型安全检查 | 全局 → 防止空指针/类型错误导致崩溃 |
| `cJSON_GetArraySize(arr)` | 获取数组长度 | 全局 → 遍历前确定边界 |
| `cJSON_Delete(obj)` | 释放 JSON 树内存 | 全局 → 每次解析完成后必须调用 |

---

## 三、JSON 解析流程（内部实现原理）

### 3.1 入口：cJSON_Parse

```
cJSON_Parse(json_string)
        │
        ▼
cJSON_ParseWithOpts(value, NULL, 0)
        │
        ▼
cJSON_ParseWithLengthOpts(value, strlen(value)+1, NULL, false)
```

### 3.2 核心解析循环

```
① skip_utf8_bom()       跳过 UTF-8 BOM（\xEF\xBB\xBF），如果存在
② buffer_skip_whitespace() 跳过空白字符（空格、\t、\r、\n）
③ parse_value()         根据首字符分派到具体类型的解析函数：

    首字符     →  解析函数
    ──────────────────────────────
    'n'        → 解析 null（匹配 "null" 4 字）
    'f'        → 解析 false（匹配 "false" 5 字）
    't'        → 解析 true（匹配 "true" 4 字）
    '"'        → parse_string()  解析字符串
    '-' / 0-9  → parse_number()  解析数字
    '['        → parse_array()   解析数组
    '{'        → parse_object()  解析对象
```

### 3.3 parse_string 原理

```
输入:  "...\"Hello\\nWorld\"..."
        │
        ├─ 第一遍扫描：计算输出长度（\" → " 缩短1字节, \\ → \ 缩短1字节, \uXXXX → UTF-8 最多4字节）
        ├─ malloc 输出缓冲区
        ├─ 第二遍处理：
        │    普通字符 → 直接复制
        │    \" \\ \/ → 去掉反斜杠
        │    \b \f \n \r \t → 转为对应的控制字符
        │    \uXXXX → utf16_literal_to_utf8() 将 UTF-16 代理对转为 UTF-8 字节序列
        └─ 末尾补 \0
```

### 3.4 parse_number 原理

```
输入:  "123.456e-7"
        │
        ├─ 扫描确定数字串边界（0-9、+、-、e、E、.）
        ├─ 拷贝到临时缓冲区
        ├─ 如果有 '.' → 替换为当前 locale 的小数点字符（国际化支持）
        ├─ strtod() → 转为 double
        ├─ 赋值 item->valuedouble
        ├─ 溢出保护：> INT_MAX → INT_MAX, < INT_MIN → INT_MIN
        └─ 设置 item->type = cJSON_Number
```

### 3.5 parse_array / parse_object 原理

两者共享相同的 do-while 结构：

```
跳过 '[' / '{'
  │
  ▼
do {
    ① cJSON_New_Item() 创建新节点
    ② 链入链表（通过 next/prev 指针）
    ③ 跳过空白
    ④ parse_value() 解析元素值（对数组）或 parse_string() 解析键名（对对象）
    ⑤ 对对象：跳过 ':'，解析值
    ⑥ 跳过空白
} while (下一个字符是 ',')
  │
  ▼
检查闭合 ']' / '}'
  │
  ▼
设置 parent->type / parent->child

嵌套深度保护：depth >= CJSON_NESTING_LIMIT(1000) → 拒绝解析（防止栈溢出）
```

---

## 四、JSON 输出（Print）流程

本项目不使用 cJSON 的输出功能（JS 导出直接用 fprintf 拼接），但 cJSON 提供了以下输出 API：

| 函数 | 输出格式 |
|------|---------|
| `cJSON_Print` | 格式化输出（缩进 + 换行） |
| `cJSON_PrintUnformatted` | 紧凑输出（无空白） |
| `cJSON_PrintBuffered` | 缓冲输出（预估大小减少 realloc） |
| `cJSON_PrintPreallocated` | 输出到预分配缓冲区 |

---

## 五、内存管理

```c
// 自定义内存分配函数（可选）
cJSON_InitHooks(&hooks);  // 设置自定义 malloc/free/realloc

// 默认使用标准库：
global_hooks.allocate   = malloc
global_hooks.deallocate = free
global_hooks.reallocate = realloc

// 释放 JSON 树
cJSON_Delete(root);  // 递归释放所有子节点和字符串
```

---

## 六、安全限制

| 限制项 | 默认值 | 作用 |
|--------|--------|------|
| `CJSON_NESTING_LIMIT` | 1000 | 最大嵌套深度（防止恶意深层嵌套 JSON 导致栈溢出） |
| `CJSON_CIRCULAR_LIMIT` | 10000 | 最大循环引用长度（防止无限递归） |

---

## 七、在项目中的位置

```
CF API 返回 JSON 字符串 (http_client 获取)
        │
        ▼
   cJSON_Parse(resp.data)
        │
        ▼
   cJSON 树（用 cJSON_GetObjectItem / cJSON_GetArrayItem 遍历提取字段）
        │
        ▼
   填充到 C 结构体（UserInfo / ContestRecord）
        │
        ▼
   cJSON_Delete(root) 释放 JSON 树
        │
        ▼
   free(resp.data) 释放 HTTP 响应体
```

---

## 八、设计特点

- **纯 C 实现**：约 2800 行有效代码，无外部依赖，单文件即可集成
- **仅用解析能力**：本项目只使用 JSON → C 结构体的解析，不使用 C → JSON 的构造（JS 导出是 fprintf 直接拼接）
- **限制**：`cf_get_user_status` 最多拉取 200 条提交（API 参数 `count=200`），对于提交量极大的用户会丢失旧数据——这是 API 层策略，非 cJSON 问题
