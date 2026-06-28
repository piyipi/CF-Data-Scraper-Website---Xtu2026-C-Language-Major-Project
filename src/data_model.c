#include "data_model.h"
#include <stdlib.h>

/*
 * userdata_free — 释放 UserData 动态内存
 * ------------------------------------------------------------------
 * 释放 UserData.records 数组的内存（由 analyzer 模块在分析阶段
 * 通过 realloc 动态分配），并重置相关字段。
 *
 * 核心逻辑：
 *   1. NULL 检查 —— 防御式编程，避免对空指针操作
 *   2. free(records) 释放动态分配的比赛记录数组
 *   3. 置 records = NULL、record_count = 0，防止 double-free 和野指针
 */
void userdata_free(UserData *ud)
{
    if (!ud) return;
    free(ud->records);
    ud->records = NULL;
    ud->record_count = 0;
}
