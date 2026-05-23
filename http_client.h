#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdlib.h>

/* 响应结构体：调用者需 free(response.data) */
typedef struct {
    char  *data;   /* 响应体内容（以 \0 结尾） */
    size_t size;   /* 响应体长度（不含 \0） */
} HttpResponse;

/* 全局初始化，程序启动时调用一次 */
void http_init(void);

/* 全局清理，程序退出前调用一次 */
void http_cleanup(void);

/*
 * GET 请求
 *   url      - 目标 URL
 *   response - 出参，存放响应内容
 * 返回值: 0 成功, -1 失败
 * 注意: 成功时 response->data 由 malloc 分配，调用者负责 free
 */
int http_get(const char *url, HttpResponse *response);

#endif /* HTTP_CLIENT_H */
