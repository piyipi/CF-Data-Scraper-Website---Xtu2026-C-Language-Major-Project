#include "http_client.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* 静态变量：仅在 http_init 后被设置 */
static int g_initialized = 0;

/* ---- write callback: 将响应片段追加到 buffer ---- */
struct WriteCtx {
    char  *data;
    size_t size;
};

/*
 * write_callback — libcurl 写回调（HTTP 响应数据累积）
 * ===================================================================
 * 注册为 libcurl 的 CURLOPT_WRITEFUNCTION。libcurl 每收到一块 HTTP 响应数据
 * 即回调本函数一次, 逐块 realloc 累积到 WriteCtx 缓冲区, 最终拼接为完整响应体。
 *
 * ---- 一、curl 库函数（本函数使用）----
 * 无——本函数由 libcurl 调用, 而非调用 libcurl。
 * 被注册方式: curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback)
 *
 * ---- 二、其他 POSIX / 系统 API ----
 * - realloc(ptr, size)  — 动态扩容累积缓冲区
 * - memcpy(dst, src, n) — 将 HTTP 响应片段追加到缓冲区尾部
 *
 * ---- 三、设计要点 ----
 * - 总量计算: len = size * nmemb。libcurl 规范要求 size 总是 1, 但防御性计算。
 * - 末尾补 \0: ctx->data[ctx->size]='\0' 保证缓冲区始终是合法 C 字符串,
 *   上层可直接传给 cJSON_Parse 无需额外拷贝。
 * - OOM 安全: realloc 失败返回 NULL 时本函数返回 0, 通知 libcurl 中止传输,
 *   避免后续访问空指针。此时 ctx->data 仍保留旧指针, 由调用者 http_get 逐次 free。
 * - 无预分配: 不预设缓冲区大小, 完全按实际响应体大小动态增长, 适应几 KB 到几十 KB 的 JSON。
 */

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct WriteCtx *ctx = (struct WriteCtx *)userdata;
    size_t len = size * nmemb;
    char *p   = (char *)realloc(ctx->data, ctx->size + len + 1);

    if (!p) return 0; /* OOM — 通知 curl 中止传输 */

    ctx->data = p;
    memcpy(ctx->data + ctx->size, ptr, len);
    ctx->size += len;
    ctx->data[ctx->size] = '\0'; /* 维持 C 字符串兼容性 */
    return len;/* 通知 curl 已成功处理 len 字节 */
}

/* ---- 公开接口 ---- */

/*
 * http_init — libcurl 全局初始化
 * ===================================================================
 * 程序启动时调用一次, 初始化 libcurl 底层子系统(SSL/TLS 引擎、DNS 解析器、
 * 协议处理器等)。必须在任何 curl_easy_* 调用之前执行。
 *
 * 调用方: main() 入口第一行, 或 interactive_mode() 入口。
 *
 * ---- 一、curl 库函数（本函数使用）----
 * - curl_global_init(CURL_GLOBAL_ALL) — 一次性初始化 libcurl 所有子模块。
 *   CURL_GLOBAL_ALL 是 CURL_GLOBAL_SSL | CURL_GLOBAL_WIN32 的组合,
 *   确保 SSL 连接和 Windows Socket 子系统均就绪。
 *
 * ---- 二、其他 POSIX / 系统 API ----
 * 无。
 *
 * ---- 三、设计要点 ----
 * - 幂等保护: 静态标志 g_initialized 防止重复调用。
 *   curl_global_init 在多线程环境非线程安全, 整个进程生命周期仅应调用一次。
 *   即使未来 main 重构导致 http_init 被多次调用也不会触发未定义行为。
 * - 与 http_cleanup 配对: Init/Cleanup 成对调用, 资源对称释放。
 * - 无返回值: 本函数无错误返回值设计(简化调用方), curl_global_init 失败
 *   概率极低且无法恢复, 唯一的选择是崩溃——由 libcurl 内部 hard-exit 处理。
 */

void http_init(void)
{
    if (!g_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        g_initialized = 1;
    }
}

/*
 * http_cleanup — libcurl 全局清理
 * ===================================================================
 * 程序退出前调用一次, 释放 libcurl 占用的所有全局资源(SSL 上下文、
 * DNS 缓存、内部连接池等)。与 http_init 配对使用, 形成对称的资源管理。
 *
 * 调用方: main() 返回前最后一行, 或 interactive_mode() 返回前。
 *
 * ---- 一、curl 库函数（本函数使用）----
 * - curl_global_cleanup() — 释放 curl_global_init 分配的所有资源。
 *
 * ---- 二、其他 POSIX / 系统 API ----
 * 无。
 *
 * ---- 三、设计要点 ----
 * - 幂等保护: g_initialized 为 0 时跳过清理, 避免"未初始化就清理"或
 *   "重复清理"导致的问题。
 * - 重置标志: 清理后将 g_initialized 置 0, 允许后续可能的重新初始化
 *   (虽然当前代码中不会, 但为未来灵活性保留)。
 * - 不释放 HttpResponse.data: 本函数只管理 libcurl 全局状态,
 *   单次请求的响应体内存在每次 http_get 后由调用者逐次 free。
 */

void http_cleanup(void)
{
    if (g_initialized) {
        curl_global_cleanup();
        g_initialized = 0;
    }
}

/*
 * http_get — HTTP GET 请求（项目数据获取的唯一网络入口）
 * ===================================================================
 * 向指定 URL 发起 HTTPS GET 请求, 带自动重试和限流处理。
 * 成功时将响应体写入 response->data (malloc 分配, 调用者负责 free)。
 * 最多重试 3 次, 3 次全部失败返回 -1。
 *
 * 这是整个项目唯一一处产生网络 I/O 的函数。cf_api 层的所有 CF API 调用
 * 最终都汇聚到本函数。
 *
 * ---- 一、curl 库函数（本函数使用）----
 * | 函数                            | 用途
 * | curl_easy_init()                | 创建 CURL 会话句柄(类似 socket 创建)
 * | curl_easy_setopt(curl, ...)     | 配置请求参数(URL / 回调 / 超时等)
 * | curl_easy_perform(curl)         | 阻塞执行网络请求(DNS→TCP→TLS→HTTP响应)
 * | curl_easy_getinfo(curl, ..., &s)| 获取传输信息(如 HTTP 状态码)
 * | curl_easy_cleanup(curl)         | 销毁 CURL 会话句柄,释放相关资源
 * | curl_easy_strerror(res)         | curl 错误码 → 可读错误字符串
 *
 * 配置参数说明:
 * - CURLOPT_URL: 目标 URL(如 "https://codeforces.com/api/user.info?...")
 * - CURLOPT_WRITEFUNCTION: 注册 write_callback 作为响应体接收回调
 * - CURLOPT_WRITEDATA: 传入 &ctx, 回调中通过 userdata 指针访问累积缓冲区
 * - CURLOPT_USERAGENT: "CF-Crawler/1.0" 标识客户端身份
 * - CURLOPT_TIMEOUT: 30L(秒) 连接+传输总超时, 防止 CF API 无响应时永久阻塞
 * - CURLOPT_FOLLOWLOCATION: 1L 自动跟随 HTTP 3xx 重定向(302→200)
 *
 * ---- 二、其他 POSIX / 系统 API ----
 * - Sleep(ms) — Windows API, 重试前等待(429 等 2s, 其余等 1s)
 * - free(ptr)  — 释放失败的响应体缓冲区
 * - fprintf(stderr, ...) — 重试/失败日志输出到 stderr
 *
 * ---- 三、设计要点 ----
 * - 每次重试新建 CURL 句柄: 不在重试间复用 curl_easy 句柄,
 *   旧句柄可能残留 SSL session / cookie 状态, 新建可避免状态污染。
 * - HTTP 429 专用等待: CF API 限流返回 429 Too Many Requests 时等待 2s,
 *   长于普通错误的 1s, 给服务端足够时间释放速率限制窗口。
 * - 所有权转移: 成功时 response->data = ctx.data 后 ctx.data = NULL,
 *   防止 curl_easy_cleanup 或重试循环误释放已移交的数据(double-free 预防)。
 * - 静默失败: 重试过程中每失败一次打印 [http] 日志到 stderr,
 *   方便排查网络问题但不打断 stdout 的正常输出流。
 * - 参数防御: url 或 response 为 NULL 直接返回 -1, 避免空指针传递到 libcurl
 *   内部引发崩溃。
 * - 30s 超时: CF API 平均响应 <1s, 30s 足够覆盖网络抖动但不至于无限等待。
 */

int http_get(const char *url, HttpResponse *response)
{
    int ret = -1;

    if (!url || !response) return -1;

    for (int attempt = 0; attempt < 3; attempt++) {
        CURL  *curl;
        CURLcode  res;
        long  status = 0;
        struct WriteCtx ctx = {NULL, 0};

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
                ctx.data = NULL;           /* 转移所有权，防止被 cleanup 释放 */
                curl_easy_cleanup(curl);
                return 0;
            }

            /* HTTP 429 Too Many Requests — CF API 限流，等久一点 */
            if (status == 429) {
                fprintf(stderr, "[http] HTTP 429 rate-limited (attempt %d/3)\n", attempt + 1);
                free(ctx.data);
                curl_easy_cleanup(curl);
                if (attempt < 2) {
                    fprintf(stderr, "[http] waiting 2s before retry...\n");
                    Sleep(2000);
                }
                continue;
            }
            fprintf(stderr, "[http] HTTP %ld for %s (attempt %d/3)\n", status, url, attempt + 1);
            free(ctx.data);
        } else {
            fprintf(stderr, "[http] curl error: %s (attempt %d/3)\n",
                    curl_easy_strerror(res), attempt + 1);
            free(ctx.data);
        }
        curl_easy_cleanup(curl);
        if (attempt < 2) {
            fprintf(stderr, "[http] waiting 1s before retry...\n");
            Sleep(1000);
        }
    }
    return ret;  /* 3 次重试全部耗尽，返回 -1 */
}
