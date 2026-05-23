#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cf_api.h"
#include "analyzer.h"
#include "http_client.h"
#include "utils.h"

/* ---- 工具 ---- */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  Single user : %s <handle>\n", prog);
    fprintf(stderr, "  Multi user  : %s <handle1> <handle2> ...\n", prog);
    fprintf(stderr, "  From file   : %s <users.txt>\n", prog);
    fprintf(stderr, "  e.g. %s tourist PetrQ\n", prog);
}

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

/* ---- 单用户处理 ---- */

static int process_single(const char *handle)
{
    UserData ud;
    if (analyze_user(handle, &ud) != 0) {
        fprintf(stderr, "Failed to analyze '%s'.\n", handle);
        return -1;
    }

    printf("  %-16s  Rating: %4d (%s)  Max: %4d  Contests: %3d\n",
           ud.info.handle, ud.info.rating, ud.info.rank_name,
           ud.info.max_rating, ud.contest_count);

    /* 导出 {handle}_data.js */
    char js_path[256];
    snprintf(js_path, sizeof(js_path), "%s_data.js", handle);
    if (export_data_js(&ud, js_path) != 0) {
        fprintf(stderr, "Failed to export %s\n", js_path);
        userdata_free(&ud);
        return -1;
    }

    /* 从模板生成 {handle}.html */
    {
        char html_path[256];
        snprintf(html_path, sizeof(html_path), "%s.html", handle);
        FILE *src = fopen("Script/template.html", "r");
        if (!src) src = fopen("template.html", "r");
        if (src) {
            FILE *dst = fopen(html_path, "w");
            if (dst) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                    fwrite(buf, 1, n, dst);
                fclose(dst);
            } else {
                fprintf(stderr, "Warning: cannot create %s\n", html_path);
            }
            fclose(src);
        } else {
            fprintf(stderr, "Warning: template.html not found\n");
        }
    }

    userdata_free(&ud);
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
    if (argc < 2) { usage(argv[0]); return 1; }

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

    printf("Processing %d user(s)...\n", hcount);

    /* 分配 UserData 数组 */
    UserData *users = (UserData *)calloc((size_t)hcount, sizeof(UserData));
    if (!users) {
        fprintf(stderr, "Out of memory\n");
        if (from_file) {
            for (int i = 0; i < hcount; i++) free(handles[i]);
            free(handles);
        }
        http_cleanup();
        return 1;
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
        if (from_file) {
            for (int i = 0; i < hcount; i++) free(handles[i]);
            free(handles);
        }
        http_cleanup();
        return 1;
    }

    if (hcount == 1) {
        /* 单用户模式：输出 data.js + index.html */
        UserData *u = &users[0];
        if (export_data_js(u, "data.js") != 0)
            fprintf(stderr, "Failed to export data.js\n");
        else
            printf("  Generated data.js\n");

        /* 生成 index.html */
        FILE *src = fopen("Script/template.html", "r");
        if (!src) src = fopen("template.html", "r");
        if (src) {
            FILE *dst = fopen("index.html", "w");
            if (dst) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                    fwrite(buf, 1, n, dst);
                fclose(dst);
                printf("  Generated index.html\n");
            }
            fclose(src);
        }
    } else {
        /* 多用户模式 */
        printf("\nGenerating per-user pages...\n");
        for (int i = 0; i < hcount; i++) {
            if (users[i].info.handle[0] == '\0') continue;
            char js_path[256], html_path[256];
            snprintf(js_path,  sizeof(js_path),  "%s_data.js", users[i].info.handle);
            snprintf(html_path, sizeof(html_path), "%s.html",     users[i].info.handle);
            export_data_js(&users[i], js_path);

            FILE *src = fopen("Script/template.html", "r");
            if (!src) src = fopen("template.html", "r");
            if (src) {
                FILE *dst = fopen(html_path, "w");
                if (dst) {
                    char buf[4096];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                        fwrite(buf, 1, n, dst);
                    fclose(dst);
                }
                fclose(src);
            }
        }

        printf("\nGenerating list page...\n");
        generate_list_page(users, hcount, "index.html");
        printf("  Generated index.html (multi-user list)\n");
    }

    /* 收尾 */
    for (int i = 0; i < hcount; i++) userdata_free(&users[i]);
    free(users);
    if (from_file) {
        for (int i = 0; i < hcount; i++) free(handles[i]);
        free(handles);
    }
    http_cleanup();

    printf("\nDone! %d/%d users processed. Open index.html in your browser.\n",
           success, hcount);
    return 0;
}
