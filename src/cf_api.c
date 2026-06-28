#include "cf_api.h"
#include "http_client.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ---- 内部辅助 ---- */

/*
 * api_get — CF API 统一请求管线（所有公开接口的内部基座）
 * ===================================================================
 * URL 组装 → HTTP GET 请求 → JSON 解析 → status=="OK" 校验,
 * 四步流水线一步到底。任何一步失败自动清理已分配资源并返回 NULL。
 * 成功返回 cJSON 根对象, 调用者需 cJSON_Delete(root) 释放。
 *
 * ---- 一、调用的 Codeforces API ----
 * 端点: https://codeforces.com/api/{method}?{params}
 * method 四种可能值: user.info | user.rating | user.status | contest.list
 * 响应格式: { "status":"OK"|"FAILED", "comment":"...", "result":{...} }
 * URL 上限: 硬编码 char url[1024]——CF API 参数简短, 溢出说明异常,
 *          直接报错而非截断, 避免生成残缺 URL。
 * params 约束: 由上层调用者组装传入, 不能包含未编码的空格或特殊字符。
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * cJSON_Parse(resp.data) —— 将 JSON 文本解析为 cJSON 树(深拷贝),
 *     独立于原始字符串。失败返回 NULL。
 * cJSON_GetObjectItem(root, key) —— 从 JSON 对象按 key 取值。
 *     key 不存在返回 NULL(不区分字段缺失和值为 null)。NULL 安全。
 * cJSON_IsString(item) —— 检查节点类型。NULL 安全, 传入 NULL 返回 0。
 * cJSON_Delete(root) —— 递归释放整个 cJSON 树。NULL 安全。
 *     必须在每个非 NULL 返回路径上配对调用, 否则内存泄漏。
 *
 * ---- 三、设计要点 ----
 * - 单一出口: 每步失败直接 return NULL, 无 goto, 无嵌套标签。
 * - 紧随释放: parse 后紧接 free(resp.data), 不拖到函数末尾。
 * - comment 透传: CF API 错误时输出 comment 原文(如 "handles: Field
 *   should contain between 3 and 24 characters..."), 方便定位问题。
 * - static 可见性: 仅 cf_api.c 内部可见, 外部必须通过 4 个公开函数
 *   间接调用 CF API。
 * - 无 API 频率控制: api_get 自身无内置限流, 频率控制由上层调用者
 *   (如 cf_get_user_status 的分页循环中 Sleep(1000))自行处理。
 */
static cJSON *api_get(const char *method, const char *params)
{
    char url[1024];
    int  n = snprintf(url, sizeof(url), "%s/%s?%s", CF_API_BASE, method, params);

    if (n < 0 || (size_t)n >= sizeof(url)) {
        fprintf(stderr, "[cf_api] URL too long for %s\n", method);
        return NULL;
    }

    HttpResponse resp = {NULL, 0};
    if (http_get(url, &resp) != 0) {
        fprintf(stderr, "[cf_api] request failed: %s\n", url);
        return NULL;
    }

    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);

    if (!root) {
        fprintf(stderr, "[cf_api] JSON parse failed for %s\n", method);
        return NULL;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!status || !cJSON_IsString(status) || strcmp(status->valuestring, "OK") != 0) {
        cJSON *comment = cJSON_GetObjectItem(root, "comment");
        fprintf(stderr, "[cf_api] API error for %s: %s\n", method,
                comment ? comment->valuestring : "unknown");
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/* ---- 公开接口 ---- */

/*
 * cf_get_user_info — 获取用户基本信息
 * ===================================================================
 * 调用 user.info API → 提取 handle/rating/maxRating/rank/avatar →
 * 填充 UserInfo 结构体。颜色通过 utils.cf_color(rating) 独立计算,
 * 不依赖 API(CF API 不返回色值)。
 *
 * ---- 一、调用的 Codeforces API ----
 * 端点: https://codeforces.com/api/user.info?handles={handle}
 * params: "handles=tourist" → snprintf(params, 128, "handles=%s", handle)
 * 响应: result 为数组, 单用户查询通常只有 1 个元素, 本函数只取 result[0]。
 * 关键字段: handle(字符串) | rating(整数) | maxRating(整数) |
 *           rank(字符串) | avatar(字符串)
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * cJSON_IsArray(node)   —— 检查节点是否为数组。NULL 安全, 传入 NULL 返回 0。
 * cJSON_GetArraySize(arr) —— 返回数组元素个数。arr 为 NULL 返回 0。
 * cJSON_GetArrayItem(arr, idx) —— 按索引取元素。越界返回 NULL。
 * cJSON_IsNumber(node)  —— 检查节点是否为数值。NULL 安全。
 * field->valueint       —— 取整数值(rating, maxRating), 已由 cJSON_IsNumber 保证安全。
 * field->valuestring    —— 取字符串指针(handle, rank, avatar), 已由 cJSON_IsString 保证安全。
 *
 * ---- 三、设计要点 ----
 * - 安全默认值: 每个字段独立校验类型, 未赋值字段保持零值/空字符串,
 *   不会因 API 响应缺失字段而读脏数据。
 * - result[0] 假定: 只取第一个元素 —— user.info?handles=tourist
 *   返回单元素数组, 项目未使用多 handle 逗号分隔查询, 故不遍历。
 * - 无 malloc: 不分配堆内存, UserInfo 所有字段为固定大小栈数组,
 *   调用者无需 free。
 * - color 独立计算: 不依赖 API 返回颜色, 通过 rating 查表(cf_color)
 *   保证颜色规则与 CF 官网严格一致。
 * - 失败原子性: 成功/失败均 cJSON_Delete(root) 后返回, root 永不泄漏。
 */
int cf_get_user_info(const char *handle, UserInfo *info)
{
    if (!handle || !info) return -1;

    char params[128];
    snprintf(params, sizeof(params), "handles=%s", handle);

    cJSON *root = api_get("user.info", params);
    if (!root) return -1;

    cJSON *arr = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
        fprintf(stderr, "[cf_api] user.info empty result for %s\n", handle);
        cJSON_Delete(root);
        return -1;
    }
    cJSON *user = cJSON_GetArrayItem(arr, 0);
    if (!user) {
        cJSON_Delete(root);
        return -1;
    }
    /* 提取字段 */
    cJSON *field;
    field = cJSON_GetObjectItem(user, "handle");
    if (cJSON_IsString(field))
        snprintf(info->handle, sizeof(info->handle), "%s", field->valuestring);

    field = cJSON_GetObjectItem(user, "rating");
    info->rating = cJSON_IsNumber(field) ? field->valueint : 0;

    field = cJSON_GetObjectItem(user, "maxRating");
    info->max_rating = cJSON_IsNumber(field) ? field->valueint : 0;

    field = cJSON_GetObjectItem(user, "rank");
    if (cJSON_IsString(field))
        snprintf(info->rank_name, sizeof(info->rank_name), "%s", field->valuestring);

    field = cJSON_GetObjectItem(user, "avatar");
    if (cJSON_IsString(field))
        snprintf(info->avatar_url, sizeof(info->avatar_url), "%s", field->valuestring);

    /* 等级分颜色 */
    snprintf(info->cf_color, sizeof(info->cf_color), "%s", cf_color(info->rating));
    
    cJSON_Delete(root);
    return 0;
}

/*
 * cf_get_user_ratings — 获取比赛 Rating 历史
 * ===================================================================
 * 调用 user.rating API → 遍历 result 数组 → 一次性 malloc 分配
 * ContestRecord 连续内存 → 逐条填充各字段。
 *
 * ---- 一、调用的 Codeforces API ----
 * 端点: https://codeforces.com/api/user.rating?handle={handle}
 * params: "handle=tourist" → snprintf(params, 128, "handle=%s", handle)
 * 响应: result 为数组(按时间从远到近排列), 每个元素对应一场比赛。
 * 关键字段: contestId(整数) | contestName(字符串) |
 *           ratingUpdateTimeSeconds(整数,Unix 时间戳,含义≈比赛结束时间) |
 *           oldRating(整数) | newRating(整数) | rank(整数) | points(浮点数)
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * cJSON_GetArraySize(arr) —— 返回数组元素个数。
 *     用于确定比赛记录数量 n, n==0 视为成功(新人无比赛记录), 返回 0。
 * cJSON_GetArrayItem(arr, i) —— 按索引取出数组元素。
 *     用于遍历比赛记录数组, 索引越界返回 NULL。
 * cJSON_IsNumber(node) —— 检查节点是否为数值类型。
 *     每个字段赋值前校验, 失败给零值默认(安全兜底)。
 * field->valuedouble —— 取浮点值(points 字段)。
 *     cJSON 使用 double 存储所有数值, valueint 用于整数, valuedouble 用于浮点。
 *
 * ---- 三、设计要点 ----
 * - 输出参数模式: records 三级指针出参, 调用者传 &recs, 函数通过
 *   *records = 写入 malloc 地址, 与 *count 配对使用。
 * - 一次 malloc: 分配 n×sizeof(ContestRecord) 连续内存, 遍历时用
 *   &(*records)[i] 定位每条记录地址, 比逐条 malloc 更快, 释放只需一次 free。
 * - memset 清零: 每条先用 memset(rec,0,sizeof(...)) 全零, 再按有效字段覆盖
 *   ——未映射字段(problem_count/upsolved 等)保持零值, 由 analyzer 后续填充。
 * - n==0 非错误: 新人无比赛记录返回 0(成功), *count=0 *records=NULL,
 *   上层检查 count 即可分支处理。
 * - 调用者释放: *records 由 userdata_free() 中的 free(ud->records) 释放,
 *   不在本函数内管理。
 * - start_time 语义: 存储 ratingUpdateTimeSeconds(Rating 更新时间≈比赛结束时间),
 *   analyzer 将其用作赛时/赛后判定分界线及近 180 天筛选基准, 注意并非
 *   比赛开始时间。
 */
int cf_get_user_ratings(const char *handle, ContestRecord **records, int *count)
{
    if (!handle || !records || !count) return -1;

    char params[128];
    snprintf(params, sizeof(params), "handle=%s", handle);

    cJSON *root = api_get("user.rating", params);
    if (!root) return -1;
    cJSON *arr = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsArray(arr)) {
        fprintf(stderr, "[cf_api] user.rating: result is not array\n");
        cJSON_Delete(root);
        return -1;
    }
    int n = cJSON_GetArraySize(arr);
    *count = 0;
    *records = NULL;
    if (n == 0) {
        cJSON_Delete(root);
        return 0;  /* 无比赛记录也算成功 */
    }
    *records = (ContestRecord *)malloc((size_t)n * sizeof(ContestRecord));
    if (!(*records)) {
        fprintf(stderr, "[cf_api] malloc failed for %d records\n", n);
        cJSON_Delete(root);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        ContestRecord *rec = &(*records)[i];
        memset(rec, 0, sizeof(ContestRecord));

        cJSON *field;

        field = cJSON_GetObjectItem(item, "contestId");
        rec->contest_id = cJSON_IsNumber(field) ? field->valueint : 0;

        field = cJSON_GetObjectItem(item, "contestName");
        if (cJSON_IsString(field))
            snprintf(rec->contest_name, sizeof(rec->contest_name), "%s", field->valuestring);

        field = cJSON_GetObjectItem(item, "ratingUpdateTimeSeconds");
        rec->start_time = cJSON_IsNumber(field) ? (time_t)field->valueint : 0;

        field = cJSON_GetObjectItem(item, "oldRating");
        rec->old_rating = cJSON_IsNumber(field) ? field->valueint : 0;

        field = cJSON_GetObjectItem(item, "newRating");
        rec->new_rating = cJSON_IsNumber(field) ? field->valueint : 0;

        field = cJSON_GetObjectItem(item, "rank");
        rec->rank = cJSON_IsNumber(field) ? field->valueint : 0;

        field = cJSON_GetObjectItem(item, "points");
        rec->points = cJSON_IsNumber(field) ? field->valuedouble : 0.0;
    }

    *count = n;
    cJSON_Delete(root);
    return 0;
}

#define CF_STATUS_PAGE_SIZE 1000

/*
 * cf_get_user_status — 获取用户提交记录（分页合并）
 * ===================================================================
 * 调用 user.status API, 自动分页拉取并合并为单一 cJSON 树返回。
 * 每页 1000 条(CF_STATUS_PAGE_SIZE), 最多 10 页(共 10000 条),
 * 页间 Sleep(1000) 遵守 CF 频率限制。
 *
 * ---- 一、调用的 Codeforces API ----
 * 端点: https://codeforces.com/api/user.status
 * params: "handle={handle}&from=1&count=1000" → 再从 from=1001 起循环追加
 * 响应: result 为提交数组, 每条含 contestId/problem/verdict/creationTimeSeconds 等字段。
 * 返回原始 cJSON 树(不做字段提取) —— 提交记录需结合 ContestRecord
 * 时间戳做赛时/赛后判别及补题匹配, 逻辑由上层 analyzer.c 在一次遍历中完成。
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * cJSON_GetObjectItem(root, key) —— 从 JSON 对象按 key 取值。
 *     字段不存在返回 NULL, 不区分"缺失"和"值为 null"。NULL 安全。
 * cJSON_IsArray(node) —— 检查节点是否为数组类型。
 *     传入 NULL 返回 0(cJSON_False), 不必先判空。
 * cJSON_GetArraySize(arr) —— 返回数组元素个数。
 *     用于判断页面是否满 1000 条, 不满则停止分页。
 * cJSON_DetachItemFromArray(arr, 0) —— 从数组中移除索引为 0 的元素,
 *     不释放节点内存, 返回被移除元素的指针。用于将分页数组逐条迁出。
 * cJSON_AddItemToArray(arr, item) —— 将元素追加到数组末尾。
 *     与 Detach 配对使用, 将逐条迁出的元素移入第 1 页的 result 数组。
 * cJSON_Delete(node) —— 递归释放整个 cJSON 树。
 *     每页处理完后 Delete 分页临时树, 只在错误路径上 Delete 主树 res。
 *
 * ---- 三、设计要点 ----
 * - 分页合并: 第 1 页的 result 数组作为"容器", 后续页用 Detach+AddItem
 *   逐条移入, 最终 *root 指向合并后的单棵 cJSON 树。上层调用者只看到
 *   一个完整的 result 数组, 分页细节完全透明。
 * - 分页终止条件: while(page_items >= 1000 && from+1000-1 <= 10000)
 *   满页且未达 CF 硬上限时继续, 任一条不满足即停止。
 * - 分页容错: 后续页网络/API 错误或返回非数组时 break 而非 return -1,
 *   已有数据保留不丢弃(多总比少好), 算部分成功。
 * - Sleep(1000): CF API 频率限制约 1 次/秒, Sleep 避免触发限流。
 * - 调用者释放: 返回后上层需 cJSON_Delete(*root) 释放合并后的树。
 * - 硬上限 10000: CF API 文档对 user.status 的 from+count 上限为 10000,
 *   超过的旧提交无法获取, 但对近 180 天分析通常足够覆盖。
 */
int cf_get_user_status(const char *handle, cJSON **root)
{
    if (!handle || !root) return -1;

    char params[256];

    /* ---- 第 1 页 ---- */
    snprintf(params, sizeof(params), "handle=%s&from=1&count=%d",
             handle, CF_STATUS_PAGE_SIZE);

    cJSON *res = api_get("user.status", params);
    if (!res) return -1;

    cJSON *result_arr = cJSON_GetObjectItem(res, "result");
    if (!cJSON_IsArray(result_arr)) {
        fprintf(stderr, "[cf_api] user.status: result is not array\n");
        cJSON_Delete(res);
        return -1;
    }

    int page_items = cJSON_GetArraySize(result_arr);
    int total      = page_items;
    int from       = CF_STATUS_PAGE_SIZE + 1;  /* 下一页起始下标 */

    /* ---- 后续分页：只要上一页是满的且未达 CF 硬上限就继续 ---- */
    while (page_items >= CF_STATUS_PAGE_SIZE
           && from + CF_STATUS_PAGE_SIZE - 1 <= 10000)
    {
        Sleep(1000);  /* CF 频率限制 */

        snprintf(params, sizeof(params), "handle=%s&from=%d&count=%d",
                 handle, from, CF_STATUS_PAGE_SIZE);

        cJSON *page = api_get("user.status", params);
        if (!page) break;  /* 网络/API 错误，保留已有数据 */

        cJSON *page_arr = cJSON_GetObjectItem(page, "result");
        if (!cJSON_IsArray(page_arr)) {
            cJSON_Delete(page);
            break;
        }

        page_items = cJSON_GetArraySize(page_arr);
        if (page_items == 0) {
            cJSON_Delete(page);
            break;
        }

        /* 将本页条目逐条移入第 1 页的 result 数组 */
        while (cJSON_GetArraySize(page_arr) > 0) {
            cJSON *item = cJSON_DetachItemFromArray(page_arr, 0);
            if (item) cJSON_AddItemToArray(result_arr, item);
        }

        total += page_items;
        cJSON_Delete(page);
        from  += CF_STATUS_PAGE_SIZE;
    }
    *root = res;
    return 0;
}
