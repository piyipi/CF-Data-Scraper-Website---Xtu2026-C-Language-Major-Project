#ifndef UTILS_H
#define UTILS_H

#include <time.h>

/* 根据等级分返回 CF 对应色值，如 "#808080" */
const char *cf_color(int rating);

/* 根据等级分返回 CF 头衔名称，如 "Newbie" */
const char *cf_rank_name(int rating);

/* 计算给定时间戳距今天数 */
int days_since(time_t t);

/* 两数取大 */
static inline int max_i(int a, int b) {
    return a > b ? a : b;
}

#endif /* UTILS_H */
