#include "utils.h"
#include <time.h>

/* CF 等级分颜色映射 */
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

/* CF 头衔名称 */
const char *cf_rank_name(int rating)
{
    if (rating <  1200) return "Newbie";
    if (rating <  1400) return "Pupil";
    if (rating <  1600) return "Specialist";
    if (rating <  1900) return "Expert";
    if (rating <  2100) return "Candidate Master";
    if (rating <  2300) return "Master";
    if (rating <  2400) return "International Master";
    if (rating <  2600) return "Grandmaster";
    if (rating <  3000) return "International Grandmaster";
    return               "Legendary Grandmaster";
}

/* 距今多少天 */
int days_since(time_t t)
{
    time_t now = time(NULL);
    double diff = difftime(now, t);
    return (int)(diff / 86400.0);
}
