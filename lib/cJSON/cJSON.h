/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#ifndef cJSON__h
#define cJSON__h

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(__WINDOWS__) && (defined(WIN32) || defined(WIN64) || defined(_MSC_VER) || defined(_WIN32))
#define __WINDOWS__
#endif

#ifdef __WINDOWS__

/* Windows 平台：指定调用约定，避免与不同默认调用约定的项目混用时出现问题。
   Windows 有三个宏定义选项：

   CJSON_HIDE_SYMBOLS  - 不想导出任何符号时定义
   CJSON_EXPORT_SYMBOLS - 构建库时导出符号（默认）
   CJSON_IMPORT_SYMBOLS - 导入符号时定义

   对于支持 visibility 属性的 *nix 平台，可以通过以下方式实现类似行为：
   在 CFLAGS 中添加 -fvisibility=hidden（gcc）或 -xldscope=hidden（sun cc），
   然后使用 CJSON_API_VISIBILITY 标志来"导出"符号，效果等同于 CJSON_EXPORT_SYMBOLS。
*/

#define CJSON_CDECL __cdecl
#define CJSON_STDCALL __stdcall

/* 默认导出符号——这对于直接复制 C 和头文件使用是必需的 */
#if !defined(CJSON_HIDE_SYMBOLS) && !defined(CJSON_IMPORT_SYMBOLS) && !defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_EXPORT_SYMBOLS
#endif

#if defined(CJSON_HIDE_SYMBOLS)
#define CJSON_PUBLIC(type)   type CJSON_STDCALL
#elif defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllexport) type CJSON_STDCALL
#elif defined(CJSON_IMPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllimport) type CJSON_STDCALL
#endif
#else /* !__WINDOWS__ */
#define CJSON_CDECL
#define CJSON_STDCALL

#if (defined(__GNUC__) || defined(__SUNPRO_CC) || defined (__SUNPRO_C)) && defined(CJSON_API_VISIBILITY)
#define CJSON_PUBLIC(type)   __attribute__((visibility("default"))) type
#else
#define CJSON_PUBLIC(type) type
#endif
#endif

/* 项目版本号 */
#define CJSON_VERSION_MAJOR 1
#define CJSON_VERSION_MINOR 7
#define CJSON_VERSION_PATCH 19

#include <stddef.h>

/* cJSON 类型定义： */
#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7) /* 原始 JSON */

#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

/* cJSON 结构体： */
typedef struct cJSON
{
    /* next/prev 允许你遍历数组/对象链。也可用 GetArraySize/GetArrayItem/GetObjectItem */
    struct cJSON *next;
    struct cJSON *prev;
    /* 数组或对象节点通过 child 指针指向其子节点链 */
    struct cJSON *child;

    /* 节点类型，取上述 cJSON_Type 值 */
    int type;

    /* 节点的字符串值（当 type==cJSON_String 或 type==cJSON_Raw 时有效） */
    char *valuestring;
    /* 直接写入 valueint 已弃用，请使用 cJSON_SetNumberValue 替代 */
    int valueint;
    /* 节点的数值（当 type==cJSON_Number 时有效） */
    double valuedouble;

    /* 节点的键名字符串——当该节点是某个对象的子节点或成员时使用 */
    char *string;
} cJSON;

typedef struct cJSON_Hooks
{
      /* malloc/free 在 Windows 上使用 CDECL 调用约定，与编译器默认约定无关 */
      void *(CJSON_CDECL *malloc_fn)(size_t sz);
      void (CJSON_CDECL *free_fn)(void *ptr);
} cJSON_Hooks;

typedef int cJSON_bool;

/* 限制嵌套深度：防止栈溢出 */
#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif

/* 限制循环引用长度：防止栈溢出 */
#ifndef CJSON_CIRCULAR_LIMIT
#define CJSON_CIRCULAR_LIMIT 10000
#endif

/* 返回 cJSON 版本号的字符串表示 */
CJSON_PUBLIC(const char*) cJSON_Version(void);

/* 为 cJSON 设置自定义的 malloc/realloc/free 函数 */
CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks);

/* 内存管理：调用者有责任释放所有 cJSON_Parse 变体的返回结果（使用 cJSON_Delete），
 * 以及 cJSON_Print 的返回结果（使用 stdlib free、cJSON_Hooks.free_fn 或 cJSON_free）。
 * 唯一的例外是 cJSON_PrintPreallocated，其缓冲区完全由调用者管理。 */
/* 传入一段 JSON 文本，返回一个可供查询的 cJSON 对象。 */
CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length);
/* ParseWithOpts 允许你要求（并检查）JSON 是以空字符结尾的，并可获取解析结束位置的指针。 */
/* 如果你提供了 return_parse_end 指针但解析失败，该指针将指向错误位置，同时 cJSON_GetErrorPtr() 也会返回相同地址。 */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated);

/* 将 cJSON 实体渲染为文本，用于传输或存储。 */
CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item);
/* 将 cJSON 实体渲染为文本（无格式化，紧凑输出）。 */
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item);
/* 使用缓冲策略渲染 cJSON 实体。prebuffer 是预估的最终大小（预估准确可减少 realloc）。fmt=0 紧凑，=1 格式化 */
CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt);
/* 将 cJSON 实体渲染到已预分配的缓冲区中。成功返回 1，失败返回 0。 */
/* 注：cJSON 对内存需求的估算并非 100% 准确，建议多分配 5 字节以保安全 */
CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const cJSON_bool format);
/* 删除 cJSON 实体及其所有子实体。 */
CJSON_PUBLIC(void) cJSON_Delete(cJSON *item);

/* 返回数组（或对象）中的元素个数。 */
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array);
/* 获取数组 array 中第 index 个元素。失败返回 NULL。 */
CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index);
/* 从对象中按键名取值。大小写不敏感。 */
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string);
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string);
/* 用于分析解析失败。返回指向解析错误位置的指针（可能需要往前看几个字符才能理解）。cJSON_Parse 返回 0 时定义，成功时为 0。 */
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void);

/* 检查节点类型并返回其值 */
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item);
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item);

/* 这些函数用于检查节点的类型 */
CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item);

/* 以下调用创建对应类型的 cJSON 节点。 */
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean);
CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num);
CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string);
/* 原始 JSON */
CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw);
CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void);

/* 创建字符串引用——valuestring 指向传入的字符串，cJSON_Delete 不会释放它 */
CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string);
/* 创建对象/数组引用——其元素不会被 cJSON_Delete 释放 */
CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child);

/* 以下工具函数创建包含 count 个元素的数组。
 * count 参数不能大于数字数组中的元素个数，否则数组访问将越界。 */
CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count);

/* 向指定的数组/对象追加元素。 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
/* 当字符串确定是常量（如字面量），且其生命周期覆盖整个 cJSON 对象时使用此函数。
 * 警告：使用此函数后，在写入 item->string 之前务必检查 (item->type & cJSON_StringIsConst) 为零。 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item);
/* 向数组/对象追加引用。当你希望将已有的 cJSON 添加到新的 cJSON 中，但不希望修改原有 cJSON 时使用。 */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item);

/* 从数组/对象中移除/分离元素。 */
CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string);

/* 更新数组元素。 */
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem); /* 将已有元素向右推移 */
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON * replacement);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object,const char *string,cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object,const char *string,cJSON *newitem);

/* 复制 cJSON 节点 */
CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse);
/* Duplicate 会创建一个与传入节点完全相同的新节点（需要释放）。
 * recurse!=0 时会递归复制所有子节点。
 * item->next 和 ->prev 指针在 Duplicate 返回时始终为零。 */
/* 递归比较两个 cJSON 节点是否相等。如果 a 或 b 为 NULL 或无效，视为不相等。
 * case_sensitive 决定对象键的比较方式：1 为大小写敏感，0 为大小写不敏感 */
CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive);

/* 压缩字符串：移除 JSON 中的空白字符（空格、制表符、回车、换行）。
 * 输入指针 json 不能指向只读地址区域（如字符串常量），应指向可读写地址。 */
CJSON_PUBLIC(void) cJSON_Minify(char *json);

/* 用于同时创建和添加元素到对象的辅助函数。
 * 返回新添加的元素，失败返回 NULL。 */
CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean);
CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number);
CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string);
CJSON_PUBLIC(cJSON*) cJSON_AddRawToObject(cJSON * const object, const char * const name, const char * const raw);
CJSON_PUBLIC(cJSON*) cJSON_AddObjectToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddArrayToObject(cJSON * const object, const char * const name);

/* 赋值整数时需要同步更新 valuedouble。 */
#define cJSON_SetIntValue(object, number) ((object) ? (object)->valueint = (object)->valuedouble = (number) : (number))
/* cJSON_SetNumberValue 宏的辅助函数 */
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number);
#define cJSON_SetNumberValue(object, number) ((object != NULL) ? cJSON_SetNumberHelper(object, (double)number) : (number))
/* 修改 cJSON_String 类型节点的 valuestring 值，仅当节点类型为 cJSON_String 时生效 */
CJSON_PUBLIC(char*) cJSON_SetValuestring(cJSON *object, const char *valuestring);

/* 如果节点不是布尔类型则不做任何操作并返回 cJSON_Invalid；否则返回新类型 */
#define cJSON_SetBoolValue(object, boolValue) ( \
    (object != NULL && ((object)->type & (cJSON_False|cJSON_True))) ? \
    (object)->type=((object)->type &(~(cJSON_False|cJSON_True)))|((boolValue)?cJSON_True:cJSON_False) : \
    cJSON_Invalid\
)

/* 遍历数组或对象的宏 */
#define cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

/* 使用通过 cJSON_InitHooks 设置的 malloc/free 函数进行内存分配/释放 */
CJSON_PUBLIC(void *) cJSON_malloc(size_t size);
CJSON_PUBLIC(void) cJSON_free(void *object);

#ifdef __cplusplus
}
#endif

#endif
