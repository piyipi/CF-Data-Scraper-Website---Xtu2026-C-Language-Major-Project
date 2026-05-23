#include "cf_api.h"
#include "http_client.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 内部辅助 ---- */

/* 组装 URL 并发送 GET 请求，返回解析后的 cJSON 根对象。
   若 API 返回 status != "OK"，fprintf 报错并返回 NULL。
   调用者需 cJSON_Delete 返回值。 */
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

int cf_get_user_status(const char *handle, cJSON **root)
{
    if (!handle || !root) return -1;

    char params[256];
    snprintf(params, sizeof(params), "handle=%s&from=1&count=200", handle);

    cJSON *res = api_get("user.status", params);
    if (!res) return -1;

    *root = res;
    return 0;
}

int cf_get_contest_list(cJSON **root)
{
    if (!root) return -1;

    cJSON *res = api_get("contest.list", "gym=false");
    if (!res) return -1;

    *root = res;
    return 0;
}
