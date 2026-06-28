#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdlib.h>

/*
 * 响应结构体: 封装 HTTP 响应体数据, 调用者需 free(response.data)。
 *
 * ---- 字段 ----
 * - data: 响应体内容, 以 \0 结尾(可直接作为 C 字符串传给 cJSON_Parse)
 * - size: 响应体真实字节数(不含末尾 \0), 用于日志/调试
 *
 * ---- 内存所有权 ----
 * http_get 成功时 data 由 write_callback 通过 realloc 分配,
 * 所有权转移给调用者。调用者用完后必须 free(resp.data),
 * 否则内存泄漏。
 */
typedef struct {
    char  *data;   /* 响应体内容（以 \0 结尾） */
    size_t size;   /* 响应体长度（不含 \0） */
} HttpResponse;

/*
 * http_init — libcurl 全局初始化。
 * 程序启动时调用一次, 初始化 SSL/TLS、DNS 等底层子系统。
 * 幂等保护: 重复调用安全(内部 g_initialized 标志防护)。
 * 必须在任何 http_get 调用之前执行。
 */
void http_init(void);

/*
 * http_cleanup — libcurl 全局清理。
 * 程序退出前调用一次, 释放 libcurl 所有全局资源。
 * 幂等保护: 未初始化或重复调用安全。
 * 与 http_init 成对使用。
 */
void http_cleanup(void);

/*
 * http_get — HTTP GET 请求（项目唯一网络 I/O 入口）。
 *
 * 参数:
 *   url      - 目标 URL(如 "https://codeforces.com/api/user.info?...")
 *   response - 出参, 成功时 data 指向 malloc 分配的完整响应体
 * 返回值:
 *    0 成功(HTTP 200), -1 失败(网络错误 / HTTP 非 200 / 3 次重试耗尽)
 *
 * 行为:
 *   - 每次重试创建全新 CURL 句柄(避免状态污染)
 *   - HTTP 429(限流) 等待 2s 后重试, 其他错误等待 1s
 *   - 30s 超时, 自动跟随重定向
 *   - 最多 3 次重试
 *
 * 注意: 成功时 response->data 由 malloc 分配, 调用者负责 free。
 */
int http_get(const char *url, HttpResponse *response);

#endif /* HTTP_CLIENT_H */
