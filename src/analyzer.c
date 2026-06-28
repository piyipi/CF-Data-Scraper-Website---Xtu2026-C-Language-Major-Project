#include "analyzer.h"
#include "cf_api.h"
#include "cJSON.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ---- analyze_user ---- */

/*
 * analyze_user — 单用户数据聚合分析（项目最核心函数）
 * ===================================================================
 * 分阶段拉取 CF API 数据并聚合为完整的 UserData:
 *   ① user.info(基本信息) → ② user.rating(比赛历史) → ③ user.status(提交记录)
 *   → ④ 补题匹配(ProbState 临时结构体) → ⑤ 难度直方图统计 → ⑥ 清理
 * 阶段③失败不中断, 跳过补题和直方图, 基本信息仍正常输出。
 *
 * ---- 一、调用的 Codeforces API（通过 cf_api 层）----
 * ① cf_get_user_info(handle, &out->info)       → user.info API
 *        失败 → return -1(基本信息缺失无继续意义)
 * ② Sleep(1500) 后 cf_get_user_ratings()       → user.rating API
 *        失败 → userdata_free(out) + return -1
 * ③ Sleep(1500) 后 cf_get_user_status()        → user.status API(分页,最多10000条)
 *        失败 → status_arr=NULL, 后续补题/直方图自动跳过, 不中断
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * cJSON_GetObjectItem(root, key) —— 按 key 取值(status_root→"result",
 *     sub→"contestId"/"problem"/"verdict"/"creationTimeSeconds"等)。
 * cJSON_IsArray(node) —— 校验提交记录数组类型。传入 NULL 返回 0。
 * cJSON_GetArraySize(arr) —— 取数组长度, 用于遍历提交记录和统计 total。
 * cJSON_GetArrayItem(arr, idx) —— 按索引取单条提交。越界返回 NULL。
 * cJSON_IsNumber(node) —— 校验 contestId/creationTimeSeconds/rating/points
 *     等数值字段。失败给零值默认(安全兜底)。
 * field->valueint —— 取整数值(contestId, rating 等)。
 * field->valuedouble —— 取浮点值(points, creationTimeSeconds 等)。
 *     cJSON 用 double 存储所有数值, valueint 取整数部分, valuedouble 取完整浮点值。
 * cJSON_IsString(node) —— 校验 verdict/problem.index 等字符串字段。
 *     NULL 安全, 传入 NULL 返回 0。
 * field->valuestring —— 取字符串指针(verdict, 题号等)。
 * cJSON_Delete(root) —— 阶段结束后释放 status_root 整个 JSON 树。
 *
 * ---- 三、设计要点 ----
 * - memset 清零入口: 函数入口 memset(out,0,sizeof(UserData)), 所有字段从零开始,
 *   后续各阶段只做增量赋值, 未触及字段保持零值(如新人无比赛→histogram全零)。
 * - 失败非对称处理: user.info 失败→return -1(根基缺失);
 *   user.rating 失败→userdata_free+return -1(无比赛数据无意义);
 *   user.status 失败→status_arr=NULL, 跳过补题/直方图, 基本信息仍输出(降级而非崩溃)。
 * - Sleep(1500): 3 个 API 调用间各等 1.5s, CF 限制约 1 次/秒, 留余量防限流。
 * - ratingUpdateTimeSeconds 作分界线: cutoff=rec->start_time,
 *   sub_time ≤ cutoff 为赛时(score+time), > cutoff 为赛后(补题判定)。
 *   注意: start_time 是 Rating 更新时间≈比赛结束时间, 非比赛开始时间。
 * - ProbState 临时结构体: 在比赛循环体内定义的 struct, 每场比赛独立初始化,
 *   跟踪各题的 attempts(赛时提交次数)/points(赛时最高分)/best_time(首次通过时间)/upsolved(是否补题)。
 * - 新旧赛制评分兼容: 有 points 字段直接用 points; 无 points 字段(旧CF赛制):
 *   OK→1.0, 非 OK→0; 多次提交取最高分(icpc风格)。
 * - 直方图手动 time_t: 阶段⑤预先 time(NULL) 一次复用, 避免内层循环每条提交都调 time()。
 * - 同一条提交多重计数: 一条 OK 提交可能同时计入 全部/365天/180天/30天
 *   4 个直方图——各时段独立统计, "近30天"是子集而非互斥。
 */
int analyze_user(const char *handle, UserData *out)
{
    if (!handle || !out) return -1;

    memset(out, 0, sizeof(UserData));

    /* ① 用户基本信息 */
    if (cf_get_user_info(handle, &out->info) != 0) {
        fprintf(stderr, "[analyzer] get_user_info failed for %s\n", handle);
        return -1;
    }
    Sleep(1500);  /* 避免 CF 限流 */
    /* ② 比赛 Rating 历史 */
    if (cf_get_user_ratings(handle, &out->records, &out->record_count) != 0) {
        fprintf(stderr, "[analyzer] get_user_ratings failed for %s\n", handle);
        userdata_free(out);
        return -1;
    }
    out->contest_count = out->record_count;
    Sleep(1500);  /* 避免 CF 限流 */
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

/*
 * escape_json_str — JS 字符串转义（静态辅助函数）
 * ===================================================================
 * 将 C 字符串转为 JS 安全的字符串常量内容, 处理 5 种转义:
 *   \ → \\   " → \"   \n → \\n   \r → \\r   \t → \\t
 * 用于 export_data_js 中比赛名称和头像 URL 的输出, 确保不破坏 JS 语法。
 *
 * ---- 一、调用的 Codeforces API ----
 * 无 —— 纯字符串处理工具函数。
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * 无。
 *
 * ---- 三、设计要点 ----
 * - 逐字符遍历: 单指针 s 遍历源串, di 追踪目标写入位置,
 *   遇特殊字符写入双字节转义序列, 普通字符直接拷贝。
 * - 防溢出: di < dst_size - 1 限制写入, 预留末位 \0。
 *   调用者传入的 dst 大小为 512 字节(char esc[512]), 比赛名称/头像 URL 远小于此。
 * - static 可见性: 仅 analyzer.c 内部使用, 被 export_data_js 调用。
 * - 零依赖: 5 种转义足够覆盖比赛名称和 URL 中的特殊字符,
 *   不引入第三方库, 保持项目纯净。
 */
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

/*
 * export_data_js — JS 数据文件导出
 * ===================================================================
 * 将 UserData 导出为 var CF_DATA = {...}; 格式的 JS 文件,
 * 供前端 HTML 通过 <script src="data.js"> 加载。用 fprintf 直接
 * 拼接 JS 而非 cJSON 序列化 —— 精确控制格式, 避免中间步骤。
 *
 * ---- 一、调用的 Codeforces API ----
 * 无 —— 输入是已填充的 UserData 结构体, 纯文件写入。
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * 无 —— 使用 fprintf 直接拼接 JS 文本, 不经过 cJSON 序列化。
 *
 * ---- 三、设计要点 ----
 * - fprintf 直接拼接: 不用 cJSON_Print, 精确控制缩进/逗号/换行,
 *   避免先构造 cJSON 树再输出的中间步骤, histogram 的 4×28=112 个
 *   整数值直接格式化输出。
 * - var CF_DATA 格式: 输出 JS 变量声明而非纯 JSON, 浏览器通过
 *   <script> 标签加载后可直接使用全局变量 CF_DATA。
 * - 比赛历史倒序: for(i=record_count-1; i>=0; i--) ——
 *   CF API 返回从远到近排列, 前端展示需要从近到远, 倒序遍历即可
 *   无需修改原数组。
 * - 转义保护: 比赛名称(contest_name)和头像 URL(avatar_url)经
 *   escape_json_str() 转义后写入, 防止 " / \ / 换行符破坏 JS 语法。
 * - histogram labels: 动态生成 "800-899" 到 "3400-3499" 共 28 个标签
 *   (HISTOGRAM_BUCKETS=28, HISTOGRAM_MIN=800, HISTOGRAM_STEP=100),
 *   与 periods 数据数组一一对应。
 * - 输出结构: handle/rating/maxRating/rank/avatar/color → contestCount/
 *   recentCount180d/recentMaxRating180d → ratingHistory[] → histogram{}。
 */
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
