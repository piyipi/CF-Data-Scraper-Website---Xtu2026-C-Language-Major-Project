# template.html —— 前端可视化层

**文件**：`web/template.html`（845 行）

**角色**：项目的"门面"。一个纯静态 HTML 页面，通过动态推导文件名加载 C 程序输出的数据（`index.html` → `data.js`，`{handle}.html` → `{handle}_data.js`），用 ECharts 渲染交互式图表。运行时由 main.c 复制并重命名为 `{handle}.html` 后分发。

---

## 一、技术栈

| 技术 | 用途 | 加载方式 |
|------|------|---------|
| HTML5 | 页面结构 | 纯手写 |
| CSS3 | 暗色主题 + 响应式布局 + 玻璃拟态风格 | 内嵌 `<style>` 标签 |
| ECharts 5.5 | Rating 折线图 + 难度直方图 | CDN 引入 |
| Vanilla JS | 数据绑定 + 图表配置 + 多时段 Tab 切换 + 窗口自适应 | 内嵌 `<script>` 标签 |

**无外部框架依赖**——仅依赖 CDN 上的 ECharts，其余全自包含。

---

## 二、页面布局架构

```
┌──────────────────────────────────────────────────────┐
│                    用户信息卡片                        │
│  [头像 36×36]  handle（CF 等级分色）                  │
│  Rating: 3774  │  Max: 4009  │  Rank: Legendary GM  │
│  Contests: 350  │  Recent(180d): 12  │  Max(180d): 3774 │
├──────────────────────────────────────────────────────┤
│                 ECharts Rating 折线图                 │
│  ┌────────────────────────────────────────────────┐  │
│  │  ╱╲    ╱╲                                    │  │
│  │ ╱  ╲╲╱  ╲    ╱╲     ★ ← 最高分标记           │  │
│  │╱         ╲╲╲╱  ╲╲╲╱                          │  │
│  │           (节点按等级分色着色)                  │  │
│  └────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────┤
│                 难度直方图（4 时段 Tab）              │
│  [All Time] [365 Days] [180 Days] [30 Days]     ← Tab│
│  ┌────────────────────────────────────────────────┐  │
│  │  ██                                            │  │
│  │  ██  ██                                        │  │
│  │  ██  ██  ██  ██                                │  │
│  │  ██  ██  ██  ██  ██  ██  ██                    │  │
│  │ 800  1200 1600 2000 2400 2800 3200 3500+       │  │
│  └────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────┤
│                   比赛详情表格                        │
│  日期 │ 赛事 │ 排名 │ 分差 │ A B C D E ... │ 补题  │
│  ─────┼──────┼──────┼──────┼───────────────┼──────  │
│  2025  │ R1000│  500 │ +100 │ ✅✅❌✅⭕   │ C     │
│  2024  │ R999 │  300 │  +50 │ ✅❌✅✅✅    │ B     │
│  ...   │ ...  │ ...  │ ...  │ ...           │ ...   │
└──────────────────────────────────────────────────────┘
```

---

## 三、CSS 设计系统

### 3.1 设计令牌（CSS Variables）

```css
:root {
  --bg-deep: #0f0f1a;           /* 深层背景色 */
  --bg-primary: #1a1a2e;        /* 卡片主背景 */
  --bg-secondary: #16213e;      /* 次级背景 */
  --bg-card: rgba(255, 255, 255, 0.035);   /* 卡片底色（半透明） */
  --bg-card-hover: rgba(255, 255, 255, 0.06); /* 卡片悬停 */
  /* ... 更多颜色、间距、阴影变量 ... */
}
```

**设计理念**：CSS 变量实现全局设计一致。修改主色调只需改一处，所有组件自动跟随。

### 3.2 暗色主题 + 玻璃拟态

```css
.card {
  background: var(--bg-card);
  backdrop-filter: blur(12px);         /* 背景模糊 */
  border: 1px solid rgba(255, 255, 255, 0.06);  /* 微透明边框 */
  border-radius: 12px;
}
```

**视觉效果**：深色背景（`#0f0f1a`）+ 半透明卡片 + 背景模糊 = 类似 macOS/iOS 的玻璃拟态风格。对长时间阅读暗色界面友好的同时，保持现代感。

### 3.3 响应式断点

| 断点 | 适用设备 | 布局变化 |
|------|---------|---------|
| ≥ 1200px | 桌面宽屏 | 完整布局，图表全宽 |
| 768–1199px | 平板 | 卡片/表格自适应缩放 |
| ≤ 480px | 手机 | 紧凑布局，字体缩小，表格横向滚动 |

**实现方式**：`@media (max-width: ...)` 查询，逐断点调整 `padding`、`font-size`、`grid` 列数。

---

## 四、JavaScript 核心逻辑

### 4.1 数据加载（动态文件名推导）

```javascript
// 根据 HTML 文件名动态推导对应数据文件名：
//   index.html     → data.js          (单用户模式)
//   {handle}.html  → {handle}_data.js  (多用户 / 交互模式)
(function() {
    var n = window.location.pathname.split('/').pop().replace('.html', '');
    var js = n === 'index' || n === '' ? 'data.js' : n + '_data.js';
    document.write('<script src="' + js + '"><\/script>');
})();
```

**数据流向**：C 程序生成 `{handle}_data.js`（或 `data.js`） → HTML 根据自身文件名推导 JS 文件名 → `document.write` 动态插入 `<script>` 标签 → 全局变量 `CF_DATA` → JS 读取并渲染。

### 4.2 用户信息卡片渲染

```javascript
function renderUserCard() {
    // 填充头像、昵称（CF 颜色）、Rating、Max、Rank、统计指标
    document.getElementById('avatar').src = data.avatar;
    document.getElementById('handle').textContent = data.handle;
    document.getElementById('handle').style.color = data.color;
    // ...
}
```

**颜色映射**：handle 的显示颜色来自 `data.color`（C 程序计算的 CF 等级分颜色），保证与 CF 官网一致。

### 4.3 Rating 折线图（ECharts）

```javascript
function renderRatingChart() {
    const chart = echarts.init(document.getElementById('rating-chart'));
    
    chart.setOption({
        xAxis: { type: 'time', ... },       // 时间轴
        yAxis: { type: 'value', ... },      // 等级分轴
        series: [{
            type: 'line',
            data: data.ratingHistory.map(r => ({
                value: [r.time * 1000, r.newRating],  // Unix 秒 → JS 毫秒
                // 节点样式
            })),
            // ...
        }],
        // ...
    });
}
```

**关键细节**：

1. **时间转换**：CF API 返回 Unix 时间戳（秒），JavaScript 需要毫秒，乘以 1000
2. **节点着色**：每个数据点根据对应等级分动态着色（灰/绿/青/蓝/紫/橙/红），与 CF 颜色体系一致
3. **极值标记**：历史最高分用 ⭐ 金色标记
4. **tooltip**：鼠标悬停显示比赛名称、时间、排名、分差

### 4.4 难度直方图（ECharts）

```javascript
function renderHistogram() {
    const chart = echarts.init(document.getElementById('histogram-chart'));
    
    function showPeriod(index) {
        chart.setOption({
            xAxis: { data: data.histogram.labels },   // X 轴：难度区间标签
            series: [{
                type: 'bar',
                data: data.histogram.periods[index].data,  // Y 轴：通过题数
            }],
        });
    }
    
    // 默认显示"全部时段"
    showPeriod(0);
    
    // Tab 切换
    document.querySelectorAll('.histogram-tab').forEach(tab => {
        tab.addEventListener('click', () => {
            showPeriod(parseInt(tab.dataset.period));  // 0=全部 1=365天 2=180天 3=30天
        });
    });
}
```

**多时段 Tab 机制**：4 个时段共享同一个 ECharts 实例——切换 Tab 时只更新 `series.data`，不重新创建图表。这保证了切换流畅且内存占用低。

### 4.5 比赛详情表格

```javascript
function renderContestTable() {
    data.ratingHistory.forEach(contest => {
        const row = document.createElement('tr');
        
        // 时间列：Unix 时间戳 → YYYY-MM-DD
        const date = new Date(contest.time * 1000).toISOString().split('T')[0];
        
        // 分差列：带 +/- 符号和颜色
        const diff = contest.newRating - contest.oldRating;
        const diffStr = (diff >= 0 ? '+' : '') + diff;
        const diffColor = diff >= 0 ? '#4caf50' : '#f44336';  // 绿涨红跌
        
        // 各题状态列：每道题一个色块
        contest.problems.forEach(prob => {
            if (prob.points > 0)       → 绿色 ✅ (通过)
            else if (prob.attempts > 0) → 红色 ❌ (尝试但未过)
            else                        → 灰色 ⭕ (未做)
        });
        
        // 补题列
        contest.upsolved.forEach(idx => {
            // 显示补题题号（如 C, D）
        });
    });
}
```

**表格特性**：
- 按时间**从近到远**排列（data.js 已倒序）
- 分差用绿涨红跌直观显示
- 每题通过状态用色块表示，一目了然
- 补题题号在独立列中展示

### 4.6 窗口自适应

```javascript
window.addEventListener('resize', () => {
    ratingChart.resize();
    histogramChart.resize();
});
```

ECharts 图表不会自动跟随窗口大小变化——需要在 `resize` 事件中手动调用 `.resize()`。两个图表实例在初始化时保存为全局变量，供此事件监听器使用。

---

## 五、与 C 程序的数据接口

```
C 程序                              前端
───────                             ────
export_data_js(ud, "output/         动态推导数据文件名
  {handle}_data.js")                   │
    │                                  ▼
    ▼                           ┌─────────────────┐
var CF_DATA = {                │ 由 HTML 自身文件名  │
    handle: "tourist",         │ 推导 JS 文件名:    │
    rating: 3774,              │ index.html         │
    ratingHistory: [...],      │   → data.js       │
    histogram: {...}           │ jiangly.html       │
};                             │   → jiangly_data.js│
                               └─────────────────┘
                                        ▼
                                全局变量 CF_DATA
                                ├─ renderUserCard()
                                ├─ renderRatingChart()
                                ├─ renderHistogram()
                                └─ renderContestTable()
```

**接口约定**：
- `data.js` 必须定义 `var CF_DATA = { ... };` 变量
- 所有字段名与 JS 端读取的字段名一致
- 时间戳在 JS 端统一乘以 1000（Unix 秒 → JS 毫秒）
- 颜色字符串直接使用 CF 等级分色值（如 `"#FF0000"`）

---

## 六、设计特点

| 设计点 | 说明 |
|--------|------|
| 模板数据分离 | HTML 不含硬编码数据，全部从 data.js 加载——同一模板服务所有用户 |
| 纯自包含 | 除 ECharts CDN 外无外部依赖，可直接离线打开（如果有本地 ECharts） |
| 暗色主题 | 深色背景 + 玻璃拟态卡片 + 彩色数据点，适合长时间浏览 |
| 单 ECharts 实例复用 | 4 时段直方图共享同一图表实例，只更新数据不重建 DOM，切换流畅 |
| 响应式 | 三断点适配桌面/平板/手机，CSS 变量 + @media 实现 |
| 零运行时依赖 | 无 Webpack/Vite/React— 纯 HTML+CSS+JS，双击即可在浏览器打开 |
