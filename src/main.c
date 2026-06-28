#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cf_api.h"
#include "analyzer.h"
#include "http_client.h"
#include "utils.h"

/* ---- 前向声明（消除 C 编译器的隐式声明警告与静态/非静态冲突） ---- */
static int str_ends_with(const char *s, const char *suffix);
static int read_handles_file(const char *path, char ***handles_out, int *count_out);
static int process_handles(char **handles, int hcount);
static int generate_list_page(UserData *users, int count, const char *filepath);

/* ---- 工具 ---- */

/*
 * interactive_mode — 交互模式入口
 * ===================================================================
 * 程序无参数启动时进入, 引导用户输入并自动识别三种格式:
 *   单 handle → 单用户 | 空格分隔多个 → 多用户 | .txt 结尾 → 文件模式
 * 解析完成后调用 process_handles 统一处理。
 *
 * ---- 一、调用的 Codeforces API ----
 * 间接 —— 通过 process_handles → analyze_user → cf_api 层。
 * 本函数只负责输入解析和模式路由, 不直接调用任何 CF API。
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * 无。
 *
 * ---- 三、设计要点 ----
 * - 三种输入自动识别: str_ends_with(input, ".txt") → 文件模式(read_handles_file);
 *   其他 → strtok 按空格分词, 单/多用户合一处理。
 * - 动态扩容: handles 数组初始 cap=16, 溢出时 realloc 翻倍, 避免固定上限。
 * - strdup 分配: 每个 handle 用 strdup 独立分配内存(来自堆), 交互模式结束时统一
 *   for(i=0;i<hcount;i++) free(handles[i]) + free(handles) 释放。
 * - strtok 跳过空白: while(*token==' ') token++ 处理 strtok 可能残留的前导空格,
 *   *token=='\0' 时继续取下一个 token, 保证空输入不污染 handles 数组。
 * - 交互提示完整: printf 打印三种格式提示, 失败或完成后 printf("Press Enter"),
 *   getchar() 等待, 双击 exe 用户友好。
 */
static int interactive_mode(void)
{
    char input[1024];
    printf("Codeforces Crawler : Interactive Mode\n");
    printf("  Single user : user\n");
    printf("  Multi user  : user1 user2 ...\n");
    printf("  From file   : users.txt\n");
    printf("\nEnter handle(s) or file: ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin)) return 1;
    input[strcspn(input, "\r\n")] = '\0';
    if (input[0] == '\0') return 1;

    /* ---- 解析输入 ---- */
    char **handles = NULL;
    int    hcount  = 0;

    if (str_ends_with(input, ".txt")) {
        /* 文件模式 */
        if (read_handles_file(input, &handles, &hcount) != 0) {
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
    } else {
        /* 空格分词 → 多用户或单用户 */
        int cap = 16;
        handles = (char **)malloc((size_t)cap * sizeof(char *));
        char *token = strtok(input, " ");
        while (token) {
            while (*token == ' ') token++;
            if (*token == '\0') { token = strtok(NULL, " "); continue; }
            if (hcount >= cap) {
                cap *= 2;
                handles = (char **)realloc(handles, (size_t)cap * sizeof(char *));
            }
            handles[hcount++] = strdup(token);
            token = strtok(NULL, " ");
        }
    }

    if (hcount == 0) {
        free(handles);
        printf("No valid handles found.\n");
        printf("\nPress Enter to exit...");
        getchar();
        return 1;
    }

    /* ---- 分析 + 输出 ---- */
    int ret = process_handles(handles, hcount);

    /* 交互模式中 handles 均由 strdup / read_handles_file 分配，需统一释放 */
    for (int i = 0; i < hcount; i++) free(handles[i]);
    free(handles);

    if (ret != 0) {
        printf("\nPress Enter to exit...");
        getchar();
        return 1;
    }

    printf("\nDone! Open output/index.html to view.\n");
    printf("\nPress Enter to exit...");
    getchar();
    return 0;
}

/*
 * process_handles — 共享处理逻辑（main.c 核心）
 * ===================================================================
 * 分析一组 handles 并生成全部输出文件。
 * 由 main() 和 interactive_mode() 共用, 消除重复代码。
 *
 * ---- 一、调用的 Codeforces API ----
 * 间接 —— 通过 analyze_user → cf_api 层。
 * 本函数只负责调度编排, 不直接调用任何 CF API。
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * 无。
 *
 * ---- 三、设计要点 ----
 * - calloc 零初始化: calloc(hcount, sizeof(UserData)) 分配并清零,
 *   analyze_user 内部又 memset 设为双重保险。
 * - 单用户失败不中断: 循环统计 success 计数, 全失败才 return -1,
 *   部分失败跳过该用户继续处理后续。
 * - 单/多用户分支: hcount==1→output/data.js+index.html(复制template.html);
 *   hcount>1→逐用户 {handle}_data.js+{handle}.html+generate_list_page()列表页。
 * - 模板两级查找: 先 web/template.html(项目目录), 回退 template.html(当前目录)。
 * - 模板缺失降级: template.html 找不到→fprintf 警告, 跳过 HTML 但不影响 data.js 输出。
 * - 4KB 缓冲复制: fread(buf,1,4096) + fwrite 逐块复制模板, 无内存膨胀。
 * - userdata_free 逐用户清理: 每个 UserData 释放其内部 records 动态数组,
 *   然后 free(users) 释放数组本身。
 * - 跳过失败用户: hcount>1 时检查 users[i].info.handle[0]=='\0' 跳过,
 *   analyze_user 失败的用户 handle 字段为空, 不生成空文件。
 */
static int process_handles(char **handles, int hcount)
{
    printf("Processing %d user(s)...\n", hcount);

    UserData *users = (UserData *)calloc((size_t)hcount, sizeof(UserData));
    if (!users) {
        fprintf(stderr, "Out of memory\n");
        return -1;
    }

    int success = 0;
    for (int i = 0; i < hcount; i++) {
        printf("[%d/%d] %s ...\n", i + 1, hcount, handles[i]);
        if (analyze_user(handles[i], &users[i]) == 0) {
            success++;
        } else {
            fprintf(stderr, "  FAILED: %s\n", handles[i]);
        }
    }

    if (success == 0) {
        fprintf(stderr, "All users failed.\n");
        free(users);
        return -1;
    }

    if (hcount == 1) {
        /* 单用户：输出 data.js + index.html */
        UserData *u = &users[0];
        if (export_data_js(u, "output/data.js") != 0)
            fprintf(stderr, "Failed to export output/data.js\n");
        else
            printf("  Generated output/data.js\n");

        FILE *src = fopen("web/template.html", "r");
        if (!src) src = fopen("template.html", "r");
        if (src) {
            FILE *dst = fopen("output/index.html", "w");
            if (dst) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                    fwrite(buf, 1, n, dst);
                fclose(dst);
                printf("  Generated output/index.html\n");
            }
            fclose(src);
        }
    } else {
        /* 多用户：逐用户页 + 列表页 */
        printf("\nGenerating per-user pages...\n");
        for (int i = 0; i < hcount; i++) {
            if (users[i].info.handle[0] == '\0') continue;
            char js_path[256], html_path[256];
            snprintf(js_path,  sizeof(js_path),  "output/%s_data.js", users[i].info.handle);
            snprintf(html_path, sizeof(html_path), "output/%s.html",     users[i].info.handle);
            export_data_js(&users[i], js_path);
            printf("  Generated %s\n", js_path);

            FILE *src = fopen("web/template.html", "r");
            if (!src) src = fopen("template.html", "r");
            if (src) {
                FILE *dst = fopen(html_path, "w");
                if (dst) {
                    char buf[4096];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                        fwrite(buf, 1, n, dst);
                    fclose(dst);
                    printf("  Generated %s\n", html_path);
                }
                fclose(src);
            }
        }

        printf("\nGenerating list page...\n");
        generate_list_page(users, hcount, "output/index.html");
        printf("  Generated output/index.html (multi-user list)\n");
    }

    for (int i = 0; i < hcount; i++) userdata_free(&users[i]);
    free(users);
    printf("\nDone! %d/%d users processed.\n", success, hcount);
    return 0;
}

/*
 * str_ends_with — 后缀匹配判断（文件模式识别）
 * ===================================================================
 * 判断字符串 s 是否以 suffix 结尾。用于识别 .txt 文件模式输入:
 *   交互模式: str_ends_with(input, ".txt") → 文件模式分支
 *   命令行:   str_ends_with(argv[1], ".txt") → 文件模式分支
 *
 * ---- 一、调用的 Codeforces API ----
 * 无 —— 纯字符串工具函数, 不涉及网络请求。
 *
 * ---- 二、cJSON 库函数（本函数使用）----
 * 无。
 *
 * ---- 三、设计要点 ----
 * - 先比较长度: sufl > sl → 直接返回 0, 避免指针偏移时 size_t 下溢
 *   (sl - sufl 在无符号算术中会回绕为极大值, 导致非法内存访问)。
 * - 指针偏移 + strcmp: s + sl - sufl 定位到原串末尾与 suffix 等长的起始位置,
 *   一次 strcmp 精确匹配该切片, O(n) 时间零额外内存。
 * - static 限定: 文件内部使用, 不暴露到头文件, 避免与其他模块同名函数符号冲突。
 * - 通用后缀: 实现为通用函数(任意后缀均可), 虽然当前仅 .txt 触发,
 *   但保持接口通用性利于后续扩展(如 .csv 等)。
 */
static int str_ends_with(const char *s, const char *suffix)
{
    size_t sl = strlen(s), sufl = strlen(suffix);
    if (sufl > sl) return 0;
    return strcmp(s + sl - sufl, suffix) == 0;
}

/* 从文件中读取 handles，每行一个，忽略空行和 # 开头的注释行。
   返回 malloc 分配的字符串数组，*handles 和每条字符串都需 free。 */
static int read_handles_file(const char *path, char ***handles_out, int *count_out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", path);
        return -1;
    }

    int cap = 16, cnt = 0;
    char **handles = (char **)malloc((size_t)cap * sizeof(char *));
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        /* 去除换行 */
        line[strcspn(line, "\r\n")] = '\0';
        /* 跳过空行和注释 */
        if (line[0] == '\0' || line[0] == '#') continue;
        /* 去除首尾空格 */
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s) - 1;
        while (e > s && (*e == ' ' || *e == '\t')) *e-- = '\0';
        if (*s == '\0') continue;

        if (cnt >= cap) {
            cap *= 2;
            handles = (char **)realloc(handles, (size_t)cap * sizeof(char *));
        }
        handles[cnt] = strdup(s);
        cnt++;
    }
    fclose(fp);

    *handles_out = handles;
    *count_out   = cnt;
    return 0;
}

/* ---- 多用户列表页生成 ---- */

static int generate_list_page(UserData *users, int count, const char *filepath)
{
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", filepath);
        return -1;
    }

    /* 计算表格最大 rating 用于着色 */
    int maxr = 0;
    for (int i = 0; i < count; i++)
        if (users[i].info.rating > maxr) maxr = users[i].info.rating;

    fprintf(fp, "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n");
    fprintf(fp, "<meta charset=\"UTF-8\">\n");
    fprintf(fp, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
    fprintf(fp, "<title>Codeforces Rating Viewer - Multi User</title>\n");
    fprintf(fp, "<style>\n");
    fprintf(fp, "  *{margin:0;padding:0;box-sizing:border-box}\n");
    fprintf(fp, "  body{background:#0f0f1a;color:#e0e0e0;font-family:'Segoe UI',sans-serif;min-height:100vh}\n");
    fprintf(fp, "  .header{background:linear-gradient(135deg,#1a1a3e,#16213e);padding:24px;"
                "text-align:center;border-bottom:2px solid #ff4757}\n");
    fprintf(fp, "  .header h1{font-size:1.8em;color:#fff}\n");
    fprintf(fp, "  .header span{color:#ff4757}.header span.y{color:#ffa502}\n");
    fprintf(fp, "  .container{max-width:1200px;margin:0 auto;padding:20px}\n");
    fprintf(fp, "  .summary{display:flex;flex-wrap:wrap;gap:16px;margin-bottom:24px}\n");
    fprintf(fp, "  .stat-card{flex:1;min-width:140px;background:rgba(255,255,255,0.04);"
                "border-radius:12px;padding:16px;text-align:center;"
                "backdrop-filter:blur(12px);border:1px solid rgba(255,255,255,0.06)}\n");
    fprintf(fp, "  .stat-card .val{font-size:1.8em;font-weight:700;font-family:'Consolas',monospace}\n");
    fprintf(fp, "  .stat-card .lbl{font-size:0.8em;color:#888;margin-top:4px}\n");
    fprintf(fp, "  table{width:100%%;border-collapse:collapse}\n");
    fprintf(fp, "  th{background:rgba(255,255,255,0.06);padding:12px 16px;text-align:left;"
                "font-size:0.85em;color:#aaa;cursor:pointer;user-select:none;"
                "border-bottom:2px solid rgba(255,255,255,0.1)}\n");
    fprintf(fp, "  th:hover{color:#fff}\n");
    fprintf(fp, "  th.sorted{color:#ffa502}\n");
    fprintf(fp, "  td{padding:12px 16px;border-bottom:1px solid rgba(255,255,255,0.04);font-size:0.9em}\n");
    fprintf(fp, "  tr.clickable{cursor:pointer;transition:background 0.15s}\n");
    fprintf(fp, "  tr.clickable:hover{background:rgba(255,255,255,0.06)}\n");
    fprintf(fp, "  tr.clickable:nth-child(even){background:rgba(255,255,255,0.02)}\n");
    fprintf(fp, "  tr.clickable:nth-child(even):hover{background:rgba(255,255,255,0.06)}\n");
    fprintf(fp, "  .avatar{width:36px;height:36px;border-radius:50%%;vertical-align:middle;margin-right:8px}\n");
    fprintf(fp, "  .rating{font-weight:700;font-family:'Consolas',monospace}\n");
    fprintf(fp, "  .footer{text-align:center;padding:24px;color:#555;font-size:0.8em}\n");
    fprintf(fp, "  @media(max-width:768px){.container{padding:12px}"
                "th,td{padding:8px 10px;font-size:0.8em}}\n");
    fprintf(fp, "</style>\n</head>\n<body>\n");
    fprintf(fp, "<div class=\"header\"><h1><span>C</span>ode<span class=\"y\">f</span>orces"
                " Rating Viewer</h1><p style=\"color:#aaa;margin-top:4px\">Multi-User Summary</p></div>\n");
    fprintf(fp, "<div class=\"container\">\n");

    /* 统计卡片 */
    int total_users = count, total_contests = 0, max_overall = 0;
    for (int i = 0; i < count; i++) {
        total_contests += users[i].contest_count;
        if (users[i].info.max_rating > max_overall) max_overall = users[i].info.max_rating;
    }
    fprintf(fp, "<div class=\"summary\">\n");
    fprintf(fp, "  <div class=\"stat-card\"><div class=\"val\">%d</div><div class=\"lbl\">Users</div></div>\n",
            total_users);
    fprintf(fp, "  <div class=\"stat-card\"><div class=\"val\">%d</div><div class=\"lbl\">Total Contests</div></div>\n",
            total_contests);
    fprintf(fp, "  <div class=\"stat-card\"><div class=\"val\" style=\"color:%s\">%d</div>"
                "<div class=\"lbl\">Highest Max Rating</div></div>\n",
            cf_color(max_overall), max_overall);
    fprintf(fp, "</div>\n");

    /* 用户表格 */
    fprintf(fp, "<table id=\"userTable\"><thead><tr>\n");
    fprintf(fp, "  <th onclick=\"sortTable(0)\">#</th>\n");
    fprintf(fp, "  <th onclick=\"sortTable(1)\">Handle</th>\n");
    fprintf(fp, "  <th onclick=\"sortTable(2)\" class=\"sorted\">Rating &#9660;</th>\n");
    fprintf(fp, "  <th onclick=\"sortTable(3)\">Max</th>\n");
    fprintf(fp, "  <th onclick=\"sortTable(4)\">Rank</th>\n");
    fprintf(fp, "  <th onclick=\"sortTable(5)\">Contests</th>\n");
    fprintf(fp, "  <th onclick=\"sortTable(6)\">Recent 180d</th>\n");
    fprintf(fp, "  <th onclick=\"sortTable(7)\">Recent Max</th>\n");
    fprintf(fp, "</tr></thead><tbody>\n");

    for (int i = 0; i < count; i++) {
        UserData *u = &users[i];
        fprintf(fp, "<tr class=\"clickable\" onclick=\"window.location='%s.html'\">\n",
                u->info.handle);
        fprintf(fp, "  <td>%d</td>\n", i + 1);
        fprintf(fp, "  <td><img class=\"avatar\" src=\"%s\" alt=\"\">"
                    "<span style=\"color:%s;font-weight:600\">%s</span></td>\n",
                u->info.avatar_url, u->info.cf_color, u->info.handle);
        fprintf(fp, "  <td><span class=\"rating\" style=\"color:%s\">%d</span></td>\n",
                u->info.cf_color, u->info.rating);
        fprintf(fp, "  <td><span class=\"rating\" style=\"color:%s\">%d</span></td>\n",
                cf_color(u->info.max_rating), u->info.max_rating);
        fprintf(fp, "  <td>%s</td>\n", u->info.rank_name);
        fprintf(fp, "  <td>%d</td>\n", u->contest_count);
        fprintf(fp, "  <td>%d</td>\n", u->recent_count_180d);
        fprintf(fp, "  <td>%d</td>\n", u->recent_max_rating_180d);
        fprintf(fp, "</tr>\n");
    }

    fprintf(fp, "</tbody></table>\n");
    fprintf(fp, "</div>\n");

    /* 排序脚本 */
    fprintf(fp, "<script>\n");
    fprintf(fp, "function sortTable(col){var t=document.getElementById('userTable');"
                "var tb=t.tBodies[0];var rows=Array.from(tb.rows);"
                "var asc=t.getAttribute('data-sort')!=col+''||t.getAttribute('data-dir')!='asc';"
                "t.setAttribute('data-sort',col);t.setAttribute('data-dir',asc?'asc':'desc');");
    /* 移除所有 sorted 标记 */
    fprintf(fp, "t.querySelectorAll('th').forEach(function(th){th.classList.remove('sorted');});");
    /* 标记当前排序列 */
    fprintf(fp, "t.querySelectorAll('th')[col].classList.add('sorted');");
    fprintf(fp, "var arrow=asc?' &#9650;':' &#9660;';"
                "t.querySelectorAll('th')[col].innerHTML=t.querySelectorAll('th')[col].innerHTML.replace(/[▲▼]/g,'')+arrow;");
    fprintf(fp, "rows.sort(function(a,b){var va=a.cells[col].textContent.trim();"
                "var vb=b.cells[col].textContent.trim();"
                "var na=parseFloat(va),nb=parseFloat(vb);"
                "if(!isNaN(na)&&!isNaN(nb))return asc?na-nb:nb-na;"
                "return asc?va.localeCompare(vb):vb.localeCompare(va);});");
    fprintf(fp, "rows.forEach(function(r){tb.appendChild(r)});}</script>\n");

    fprintf(fp, "<div class=\"footer\">Generated %s | Powered by Codeforces API | "
                "<a style=\"color:#ff4757\" href=\"https://codeforces.com\">Codeforces</a></div>\n",
            __DATE__);
    fprintf(fp, "</body>\n</html>\n");

    fclose(fp);
    return 0;
}

/* ================================================================== */

int main(int argc, char *argv[])
{
    /* 双击启动（无参数）→ 交互模式 */
    if (argc < 2) {
        http_init();
        int ret = interactive_mode();
        http_cleanup();
        return ret;
    }

    http_init();

    /* 判断模式 */
    char **handles = NULL;
    int    hcount  = 0;
    int    from_file = 0;

    if (argc == 2 && str_ends_with(argv[1], ".txt")) {
        /* 文件模式 */
        if (read_handles_file(argv[1], &handles, &hcount) != 0) {
            http_cleanup();
            return 1;
        }
        from_file = 1;
    } else if (argc >= 3) {
        /* 多参数模式 */
        hcount  = argc - 1;
        handles = &argv[1];
    } else {
        /* 单用户模式 */
        hcount  = 1;
        handles = &argv[1];
    }

    int ret = process_handles(handles, hcount);

    if (from_file) {
        for (int i = 0; i < hcount; i++) free(handles[i]);
        free(handles);
    }
    http_cleanup();
    return (ret == 0) ? 0 : 1;
}
