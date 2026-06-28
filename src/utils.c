#include "utils.h"
#include <time.h>

/*
 * cf_color — CF 等级分 → 颜色十六进制值
 * ------------------------------------------------------------------
 * 根据 Codeforces 官方颜色体系，将等级分映射为对应的十六进制
 * 颜色值，用于前端页面和 ECharts 图表中用户昵称、分数节点的着色。
 *
 * 核心逻辑：8 级阶梯式区间匹配（if-else 链），完全遵循 CF 官方色彩：
 *   < 1200  → #808080 灰   (Newbie)
 *   1200    → #008000 绿   (Pupil)
 *   1400    → #03A89E 青   (Specialist)
 *   1600    → #0000FF 蓝   (Expert)
 *   1900    → #AA00AA 紫   (Candidate Master)
 *   2100    → #FF8C00 橙   (Master / International Master)
 *   2400    → #FF0000 红   (Grandmaster / International Grandmaster)
 *   >= 3000 → #CC0000 暗红 (Legendary Grandmaster)
 */
const char *cf_color(int rating)
{
    if (rating <  1200) return "#808080";  /* Newbie 灰 */
    if (rating <  1400) return "#008000";  /* Pupil 绿 */
    if (rating <  1600) return "#03A89E";  /* Specialist 青 */
    if (rating <  1900) return "#0000FF";  /* Expert 蓝 */
    if (rating <  2100) return "#AA00AA";  /* Candidate Master 紫 */
    if (rating <  2400) return "#FF8C00";  /* Master / IM 橙 */
    if (rating <  3000) return "#FF0000";  /* Grandmaster / IGM 红 */
    return               "#CC0000";        /* Legendary Grandmaster 暗红 */
}
/*
 * days_since — 计算距今多少天
 * ------------------------------------------------------------------
 * 返回从给定 Unix 时间戳到当前时刻经过的天数（截断取整）。
 * 用于筛选"近 180 天"、"近一年"等时间窗口内的比赛记录。
 *
 * 核心逻辑：
 *   1. time(NULL) 获取当前 Unix 时间戳
 *   2. difftime(now, t) 计算秒数差 —— 使用 difftime 而非直接
 *      相减，因为 C 标准未规定 time_t 的确切类型，直接相减可能
 *      因符号/溢出问题导致不可移植行为
 *   3. 除以 86400.0（一天 86400 秒）并截断取整为天数
 *
 * 注意：结果类型为 int，截断取整（非四舍五入），23 小时算 0 天。
 */
int days_since(time_t t)
{
    time_t now = time(NULL);
    double diff = difftime(now, t);
    return (int)(diff / 86400.0);
}
