#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <time.h>

/* ---- 单题结果 ---- */
typedef struct {
    char   index[4];       /* 题号，如 "A"、"B1"、"C" */
    double points;         /* 该题得分 */
    int    attempt_count;  /* 提交次数（含失败） */
    int    best_time;      /* 首次通过时间（秒，距比赛开始），未通过则为 -1 */
} ProblemResult;

/* ---- 用户基本信息 ---- */
typedef struct {
    char handle[64];        /* 用户名 */
    int  rating;            /* 当前等级分 */
    int  max_rating;        /* 历史最高等级分 */
    char rank_name[32];     /* 头衔，如 "Expert" */
    char avatar_url[256];   /* 头像图片地址 */
    char cf_color[16];      /* 当前等级分对应色值，如 "#0000FF" */
} UserInfo;

/* ---- 单场比赛记录 ---- */
#define MAX_PROBLEMS 12

typedef struct {
    int    contest_id;         /* CF 比赛 ID */
    char   contest_name[256];  /* 比赛名称 */
    time_t start_time;         /* 比赛开始时间（Unix 时间戳） */
    int    old_rating;         /* 赛前等级分 */
    int    new_rating;         /* 赛后等级分 */
    int    rank;               /* 比赛排名 */
    double points;             /* 总分 */
    int    problem_count;      /* 题目数量 */
    ProblemResult problems[MAX_PROBLEMS]; /* 各题详情 */
    int    upsolved_count;              /* 赛后补题数 */
    char   upsolved[MAX_PROBLEMS][4];    /* 补做题号 */
} ContestRecord;

/* ---- 难度直方图 ---- */
#define HISTOGRAM_PERIODS 4    /* 全部 / 一年 / 180天 / 30天 */
#define HISTOGRAM_BUCKETS 28   /* 800-3500, 步长 100 */
#define HISTOGRAM_MIN     800
#define HISTOGRAM_STEP    100

/* ---- 用户聚合数据 ---- */
typedef struct {
    UserInfo info;                       /* 基本信息 */
    int      contest_count;              /* 历史总比赛次数 */
    int      recent_count_180d;          /* 近 180 天比赛次数 */
    int      recent_max_rating_180d;     /* 近 180 天最高等级分 */
    ContestRecord *records;              /* 比赛记录数组（动态分配） */
    int      record_count;               /* 记录数量 */
    int      histogram[HISTOGRAM_PERIODS][HISTOGRAM_BUCKETS]; /* 难度直方图 */
} UserData;

/* ---- 释放 UserData 中动态分配的内存 ---- */
void userdata_free(UserData *ud);

#endif /* DATA_MODEL_H */
