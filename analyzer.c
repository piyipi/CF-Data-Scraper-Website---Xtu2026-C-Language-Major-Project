#include "analyzer.h"
#include "cf_api.h"
#include "cJSON.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- analyze_user ---- */

int analyze_user(const char *handle, UserData *out)
{
    if (!handle || !out) return -1;

    memset(out, 0, sizeof(UserData));

    /* ① 用户基本信息 */
    if (cf_get_user_info(handle, &out->info) != 0) {
        fprintf(stderr, "[analyzer] get_user_info failed for %s\n", handle);
        return -1;
    }

    /* ② 比赛 Rating 历史 */
    if (cf_get_user_ratings(handle, &out->records, &out->record_count) != 0) {
        fprintf(stderr, "[analyzer] get_user_ratings failed for %s\n", handle);
        userdata_free(out);
        return -1;
    }

    out->contest_count = out->record_count;

    /* ③ 提交记录 */
    cJSON *status_root = NULL;
    cJSON *status_arr  = NULL;
    if (cf_get_user_status(handle, &status_root) == 0) {
        status_arr = cJSON_GetObjectItem(status_root, "result");
        if (!cJSON_IsArray(status_arr)) status_arr = NULL;
    }

    /* ④ 统计 + 补题匹配 */
    for (int i = 0; i < out->record_count; i++) {
        ContestRecord *rec = &out->records[i];
        int cid = rec->contest_id;
        time_t cutoff = rec->start_time;  /* ratingUpdateTimeSeconds，作为赛时/赛后分界线 */

        /* 近 180 天统计 */
        if (days_since(rec->start_time) <= 180) {
            out->recent_count_180d++;
            if (rec->new_rating > out->recent_max_rating_180d)
                out->recent_max_rating_180d = rec->new_rating;
        }

        if (!status_arr || cid == 0) continue;

        /* 遍历提交记录，匹配当前比赛 */
        int arr_size = cJSON_GetArraySize(status_arr);
        /* 临时结构：跟踪每个题号的状态 */
        struct ProbState {
            char   index[4];
            int    attempts;       /* 赛时提交次数 */
            double points;         /* 赛时最高分 */
            int    best_time;      /* 赛时首次通过时间 */
            int    upsolved;       /* 赛后补题 */
        };
        struct ProbState ps[MAX_PROBLEMS];
        memset(ps, 0, sizeof(ps));
        int ps_count = 0;  /* 实际遇到的题号数量 */

        for (int j = 0; j < arr_size; j++) {
            cJSON *sub = cJSON_GetArrayItem(status_arr, j);

            /* 检查 contestId 匹配 */
            cJSON *f = cJSON_GetObjectItem(sub, "contestId");
            if (!cJSON_IsNumber(f) || f->valueint != cid) continue;

            /* 获取题号 */
            cJSON *prob = cJSON_GetObjectItem(sub, "problem");
            if (!prob) continue;
            cJSON *idx = cJSON_GetObjectItem(prob, "index");
            if (!cJSON_IsString(idx)) continue;

            const char *pid = idx->valuestring;

            /* 获取 verdict */
            cJSON *verdict = cJSON_GetObjectItem(sub, "verdict");
            const char *v = cJSON_IsString(verdict) ? verdict->valuestring : "";

            /* 获取时间戳 */
            cJSON *ts = cJSON_GetObjectItem(sub, "creationTimeSeconds");
            long sub_time = cJSON_IsNumber(ts) ? (long)ts->valuedouble : 0;

            int is_ok = (strcmp(v, "OK") == 0);

            /* 查找或创建该题号的状态记录 */
            int pi = -1;
            for (int k = 0; k < ps_count; k++) {
                if (strcmp(ps[k].index, pid) == 0) { pi = k; break; }
            }
            if (pi == -1 && ps_count < MAX_PROBLEMS) {
                pi = ps_count++;
                snprintf(ps[pi].index, sizeof(ps[pi].index), "%s", pid);
            }
            if (pi == -1) continue;  /* 超出 MAX_PROBLEMS */

            if (sub_time <= cutoff) {
                /* 赛时提交 */
                ps[pi].attempts++;

                /* 取最高分 */
                cJSON *fp = cJSON_GetObjectItem(sub, "points");
                double pts = cJSON_IsNumber(fp) ? fp->valuedouble : 0.0;
                /* 如果没有 points，按通过/未通过给分 */
                if (pts == 0.0 && !is_ok) pts = 0.0;
                if (pts == 0.0 && is_ok)  pts = 1.0;
                if (pts > ps[pi].points) ps[pi].points = pts;

                /* 首次通过时间 */
                if (is_ok && ps[pi].best_time == 0)
                    ps[pi].best_time = (int)(sub_time - cutoff);

            } else {
                /* 赛后提交：补题 */
                if (is_ok && !ps[pi].upsolved)
                    ps[pi].upsolved = 1;
            }
        }

        /* ⑤ 回填 ContestRecord */
        rec->problem_count = 0;
        rec->upsolved_count = 0;
        for (int k = 0; k < ps_count; k++) {
            if (rec->problem_count >= MAX_PROBLEMS) break;
            ProblemResult *pr = &rec->problems[rec->problem_count];
            snprintf(pr->index, sizeof(pr->index), "%s", ps[k].index);
            pr->points        = ps[k].points;
            pr->attempt_count = ps[k].attempts;
            pr->best_time     = ps[k].best_time ? ps[k].best_time : -1;
            rec->problem_count++;

            if (ps[k].upsolved) {
                snprintf(rec->upsolved[rec->upsolved_count],
                         sizeof(rec->upsolved[0]), "%s", ps[k].index);
                rec->upsolved_count++;
            }
        }
    }

    /* ⑥ 难度直方图统计 */
    if (status_arr) {
        time_t now = time(NULL);
        int arr_size = cJSON_GetArraySize(status_arr);
        for (int j = 0; j < arr_size; j++) {
            cJSON *sub = cJSON_GetArrayItem(status_arr, j);

            /* 只统计 OK 提交 */
            cJSON *verdict = cJSON_GetObjectItem(sub, "verdict");
            if (!cJSON_IsString(verdict) || strcmp(verdict->valuestring, "OK") != 0) continue;

            /* 获取难度 */
            cJSON *prob = cJSON_GetObjectItem(sub, "problem");
            if (!prob) continue;
            cJSON *rating = cJSON_GetObjectItem(prob, "rating");
            if (!cJSON_IsNumber(rating)) continue;
            int diff = rating->valueint;

            /* 难度分桶 */
            int bucket = (diff - HISTOGRAM_MIN) / HISTOGRAM_STEP;
            if (bucket < 0) bucket = 0;
            if (bucket >= HISTOGRAM_BUCKETS) bucket = HISTOGRAM_BUCKETS - 1;

            /* 获取提交时间 */
            cJSON *ts = cJSON_GetObjectItem(sub, "creationTimeSeconds");
            long sub_time = cJSON_IsNumber(ts) ? (long)ts->valuedouble : 0;
            double days_ago = difftime(now, (time_t)sub_time) / 86400.0;

            out->histogram[0][bucket]++;  /* 全部 */
            if (days_ago <= 365) out->histogram[1][bucket]++;  /* 近一年 */
            if (days_ago <= 180) out->histogram[2][bucket]++;  /* 近 180 天 */
            if (days_ago <= 30)  out->histogram[3][bucket]++;  /* 近 30 天 */
        }
    }

    if (status_root) cJSON_Delete(status_root);
    return 0;
}

/* ---- export_data_js ---- */

static void escape_json_str(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (const char *s = src; *s && di < dst_size - 1; s++) {
        if      (*s == '\\') { dst[di++] = '\\'; dst[di++] = '\\'; }
        else if (*s == '"')  { dst[di++] = '\\'; dst[di++] = '"';  }
        else if (*s == '\n') { dst[di++] = '\\'; dst[di++] = 'n';  }
        else if (*s == '\r') { dst[di++] = '\\'; dst[di++] = 'r';  }
        else if (*s == '\t') { dst[di++] = '\\'; dst[di++] = 't';  }
        else                   dst[di++] = *s;
    }
    dst[di] = '\0';
}

int export_data_js(const UserData *ud, const char *filepath)
{
    if (!ud || !filepath) return -1;

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        fprintf(stderr, "[analyzer] cannot open %s\n", filepath);
        return -1;
    }

    char esc[512];

    fprintf(fp, "var CF_DATA = {\n");

    /* 基本信息 */
    fprintf(fp, "  handle: \"%s\",\n", ud->info.handle);
    fprintf(fp, "  rating: %d,\n", ud->info.rating);
    fprintf(fp, "  maxRating: %d,\n", ud->info.max_rating);
    fprintf(fp, "  rank: \"%s\",\n", ud->info.rank_name);

    escape_json_str(ud->info.avatar_url, esc, sizeof(esc));
    fprintf(fp, "  avatar: \"%s\",\n", esc);

    fprintf(fp, "  color: \"%s\",\n", ud->info.cf_color);
    fprintf(fp, "  contestCount: %d,\n", ud->contest_count);
    fprintf(fp, "  recentCount180d: %d,\n", ud->recent_count_180d);
    fprintf(fp, "  recentMaxRating180d: %d,\n", ud->recent_max_rating_180d);

    /* 比赛历史（时间从近到远） */
    fprintf(fp, "  ratingHistory: [\n");
    for (int i = ud->record_count - 1; i >= 0; i--) {
        const ContestRecord *rec = &ud->records[i];
        escape_json_str(rec->contest_name, esc, sizeof(esc));
        fprintf(fp, "    { contestName: \"%s\", time: %ld, rank: %d,"
                    " oldRating: %d, newRating: %d, points: %.0f,"
                    " problems: [",
                esc, (long)rec->start_time, rec->rank,
                rec->old_rating, rec->new_rating, rec->points);

        /* 各题得分 */
        for (int p = 0; p < rec->problem_count; p++) {
            fprintf(fp, "%s{ index:\"%s\", points:%.0f, attempts:%d, bestTime:%d }",
                    p > 0 ? "," : "",
                    rec->problems[p].index, rec->problems[p].points,
                    rec->problems[p].attempt_count, rec->problems[p].best_time);
        }
        fprintf(fp, "], upsolved: [");

        /* 补题 */
        for (int u = 0; u < rec->upsolved_count; u++) {
            fprintf(fp, "%s\"%s\"", u > 0 ? "," : "", rec->upsolved[u]);
        }

        fprintf(fp, "] }%s\n", i > 0 ? "," : "");
    }
    fprintf(fp, "  ]\n");

    /* 难度直方图 */
    fprintf(fp, "  ,histogram: {\n");
    fprintf(fp, "    min: %d,\n", HISTOGRAM_MIN);
    fprintf(fp, "    step: %d,\n", HISTOGRAM_STEP);
    fprintf(fp, "    labels: [");
    for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
        fprintf(fp, "%s\"%d-%d\"", i > 0 ? "," : "",
                HISTOGRAM_MIN + i * HISTOGRAM_STEP,
                HISTOGRAM_MIN + (i + 1) * HISTOGRAM_STEP - 1);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "    periods: [\n");
    const char *period_names[] = {"All Time", "365 Days", "180 Days", "30 Days"};
    for (int p = 0; p < HISTOGRAM_PERIODS; p++) {
        fprintf(fp, "      { name: \"%s\", data: [", period_names[p]);
        for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
            fprintf(fp, "%s%d", i > 0 ? "," : "", ud->histogram[p][i]);
        }
        fprintf(fp, "] }%s\n", p < HISTOGRAM_PERIODS - 1 ? "," : "");
    }
    fprintf(fp, "    ]\n");
    fprintf(fp, "  }\n");

    fprintf(fp, "};\n");

    fclose(fp);
    return 0;
}

/* ---- analyze_multiple（选作） ---- */

int analyze_multiple(const char **handles, int count, UserData *out)
{
    if (!handles || !out || count <= 0) return -1;

    for (int i = 0; i < count; i++) {
        if (analyze_user(handles[i], &out[i]) != 0) {
            fprintf(stderr, "[analyzer] analyze_multiple: failed on %s\n", handles[i]);
            return -1;
        }
    }
    return 0;
}
