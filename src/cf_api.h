#ifndef CF_API_H
#define CF_API_H

#include "data_model.h"
#include "cJSON.h"

#define CF_API_BASE "https://codeforces.com/api"

/* ===== 用户信息 ===== */
/* 调用 user.info 接口，填充 UserInfo 结构体。
   成功返回 0，失败返回 -1（网络错误/API 返回 FAILED/JSON 解析失败）。*/
int cf_get_user_info(const char *handle, UserInfo *info);

/* ===== 比赛 Rating 历史 ===== */
/* 调用 user.rating 接口，动态分配 ContestRecord 数组。
   records 为出参，count 为出参长度。
   成功返回 0，失败返回 -1。
   调用者需 free(*records)。注意：records 按时间从远到近排列。*/
int cf_get_user_ratings(const char *handle, ContestRecord **records, int *count);

/* ===== 提交记录（原始 JSON） ===== */
/* 调用 user.status 接口，分页拉取并合并所有提交记录（最多 10000 条），
    返回原始 cJSON 根对象供分析模块使用。
    成功返回 0，失败返回 -1。
    调用者需 cJSON_Delete(*root)。*/
int cf_get_user_status(const char *handle, cJSON **root);

#endif /* CF_API_H */
