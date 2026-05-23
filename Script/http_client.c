#include "http_client.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

/* 静态变量：仅在 http_init 后被设置 */
static int g_initialized = 0;

/* ---- write callback: 将响应片段追加到 buffer ---- */
struct WriteCtx {
    char  *data;
    size_t size;
};

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct WriteCtx *ctx = (struct WriteCtx *)userdata;
    size_t len   = size * nmemb;
    char *p     = (char *)realloc(ctx->data, ctx->size + len + 1);

    if (!p) return 0; /* OOM */

    ctx->data = p;
    memcpy(ctx->data + ctx->size, ptr, len);
    ctx->size        += len;
    ctx->data[ctx->size] = '\0';
    return len;
}

/* ---- 公开接口 ---- */

void http_init(void)
{
    if (!g_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        g_initialized = 1;
    }
}

void http_cleanup(void)
{
    if (g_initialized) {
        curl_global_cleanup();
        g_initialized = 0;
    }
}

int http_get(const char *url, HttpResponse *response)
{
    CURL             *curl;
    CURLcode          res;
    long              status = 0;
    struct WriteCtx   ctx    = {NULL, 0};
    int               ret    = -1;

    if (!url || !response) return -1;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[http] curl_easy_init failed\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "CF-Crawler/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (status == 200) {
            response->data = ctx.data;
            response->size = ctx.size;
            ctx.data = NULL;           /* 转移所有权 */
            ret = 0;
        } else {
            fprintf(stderr, "[http] HTTP %ld for %s\n", status, url);
            free(ctx.data);
        }
    } else {
        fprintf(stderr, "[http] curl error: %s\n", curl_easy_strerror(res));
        free(ctx.data);
    }

    curl_easy_cleanup(curl);
    return ret;
}
