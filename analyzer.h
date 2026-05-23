#ifndef ANALYZER_H
#define ANALYZER_H

#include "data_model.h"

/* 聚合分析单个用户：拉取信息 + 比赛历史 + 补题匹配 → UserData */
int analyze_user(const char *handle, UserData *out);

/* 将 UserData 导出为 JS 文件，供 HTML 页面通过 <script src="data.js"> 加载 */
int export_data_js(const UserData *ud, const char *filepath);

/* 多用户批量分析（选作） */
int analyze_multiple(const char **handles, int count, UserData *out);

#endif /* ANALYZER_H */
