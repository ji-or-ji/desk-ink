# 让 AI 参与电路设计：嘉立创EDA 的接入路径

> 调研日期 2026-09-24。目的：验证"AI 能在软硬交界做多少事"这条项目主线。
> 结论里凡是官方文档明说的都标了出处；查不到的地方写"没查到"，不拿相邻事实凑数。

## 一句话结论

**编辑器本体没有内置 AI，但官方主动把 AI 当第一类用户来建设接口。**

## 一、官方态度（文档明说）

- 官方专门有一页《For AI：扩展工程初始化指南》，面向 Claude Code / OpenCode / QwenCode / GitHub Copilot，并规定初始化扩展工程时"默认应当安装 `easyeda-api` Skill"。
  https://prodocs.lceda.cn/cn/api/guide/how-to-start-for-ai.html （更新 2026-09-07）
- 官方发布桥接扩展 **Run API Gateway**，描述是"为 AI 编程工具提供 WebSocket 网关桥接服务"，v1.0.6 / 2026-09-16。
  https://jlc-ext.com/item/oshwhub/run-api-gateway
- **本体没有 AI 入口**。V4.1.60（2026-09-21）更新记录里有"草图半自动布局""半自动布线"，那是传统算法，**不算 AI**，别记进战果。
  https://pro.lceda.cn/page/update-record

## 二、扩展能力边界

- 形式：JS / TypeScript 扩展，产物 `.eext`，在 高级 → 扩展管理器 → 导入
- SDK 在 **Gitee**（国内可达）：`gitee.com/jlceda/pro-api-sdk`；类型定义 npm 包 `@jlceda/pro-api-types`
- **能发外部请求**：`SYS_ClientUrl.request()`，官方原文"向外部服务器发起安全的 cURL 请求"
  - 两个硬限制：必须启用扩展的**外部交互权限**（未启用会永远 `throw Error`）；且被请求站点必须允许 **CORS**
- 也能走 WebSocket：`SYS_WebSocket.register / send / close`
- 文档入口：https://prodocs.lceda.cn/cn/api/reference/pro-api.html

## 三、工程文件到底是什么（一手实测）

| 文件 | 真相 |
|---|---|
| `.eprj2` | **SQLite 数据库**。文件头就是 `SQLite format 3`。表里有 `components` / `devices` / `coppers` / `boards` / `attributes` / `backups`，每行一个 `dataStr` 字段存 JSON 原子数据 |
| `.epro` | **导出的工程包**（zip）。官方导入页原文"选择 epro 或 zip 压缩包导入" |
| V3 源码格式 | **逐行 JSON**，每行两个 JSON 用 `\|\|` 拼接；键名驼峰，坐标单位 0.01 inch。https://prodocs.lceda.cn/cn/format/index/ |
| 网表 | 官方标准 **Netlist_SCH_V3.0**（JSON 2.0.0），含 `components` / `pinInfoMap` / `designRule` / `netRule` |

**直接后果**：`.eprj2` 是二进制，**git diff 基本没有意义**（每次改动都是一整个 blob，1.7 MB）。

## 四、现成的 AI 相关扩展（扩展广场可一键装）

| 扩展 | 作用 | 发布者 |
|---|---|---|
| 让AI看看你的原理图 | 导出框选电路的连接关系给任意 AI 分析，结果反向导入 | Yt禾斗rY |
| AI巧绘 | 对话式生成原理图，可填火山引擎 ARK / deepseek / GLM 的 Key | linchangyi |
| 局部原理图网表导出 | 框选后按官方格式导出 JSON 网表，README 原话就是"发给 AI 让 AI 分析该部分电路是否设计正确" | chenlong11 |
| Run API Gateway | 官方 WebSocket 桥，AI 编程工具直连 EDA 窗口 | EasyEDA |
| MCP Bridge 社区版 | 原生 MCP Server，可读网表、放器件，带 `schematic_review` 全工程审查 | hs150521 |
| EDA Agent Connector | 本地 daemon 驱动 AI 全流程画板，每步过四阶段门禁 | zhoushoujian |
| 数据手册AI助手 | 权限表里写着"外部交互权限 → 下载 PDF、调用 AI API" | EasyEDA |

## 五、可行路径（从轻到重）

| 档 | 做法 | 卡点 |
|---|---|---|
| **0** | 装《让AI看看你的原理图》→ 框选模块 → 复制提示词 → 贴给网页版 AI | 几乎没有。不动 git、不填 Key、不开权限 |
| **1** | 装《局部原理图网表导出》→ 导出官方格式网表 → 附上设计约束 → 交给 AI 审查 | AI 只看你框选的部分；审查质量取决于约束写得多清楚 |
| **2** | 官方 Run API Gateway + `easyeda-api` Skill → AI 直接读写打开着的工程 | 要跑本地 Bridge Server；扩展管理器里必须勾**允许外部交互**；需 Node.js ≥ 20.17 |
| **3** | MCP Bridge 社区版 + 原生 MCP Server | 本地服务 + 端口/token 配置；作者提醒"工具能改工程，操作前先存盘" |
| **4** | EDA Agent Connector 全流程画板 | 组件多，版本要对齐，学习成本高 |
| **5** | 自己写扩展，用 `SYS_ClientUrl` 直连 AI API | **CORS 是硬卡点**：多数大模型 API 不许浏览器端跨域直调，且 Key 放前端不安全。生态里普遍改用 WebSocket 本地桥 |

## 六、公开案例

- **《AI设计的基于NE555的声光报警器》**（OSHWHub 开源工程，2026-08-14）：用"让AI看看你的原理图" + 网页版 DeepSeek，**全程无人工干预，4 轮对话完成，已打板验证**。
  https://oshwhub.com/t0k0m/project_fjjeygwy
- **EDA Agent Connector 的 ESP32-S3 四层板案例**（2026-09）：AI 完成自动布局、板框贴合、规则感知布线、四层电源平面、丝印避让，全程回读校验；**作者如实披露官方 DRC 仍有 3 个 WARN 没过硬门禁**。

**校准预期**：NE555 是单芯片小电路；本项目是多模块系统（电源 / I2C / 墨水屏时序 / 无线），难度高一个量级，别拿"AI 能画 NE555"直接外推。

## 七、对本项目的两条直接启发

1. **AI 离线读工程完全可行。** `.eprj2` 是 SQLite，直接用任意 SQLite 库读 `dataStr` 就能拿到结构化网表——**不需要装扩展、不需要开权限、不用管 CORS**。这是本项目能自己走通的一条独立路线。
2. **git 里跟踪的 `.eprj2` 是无意义二进制。** 它随每次改动整体重写（1.7 MB）。要真正把设计纳入版本管理，可考虑改用 `.epro`（逐行 JSON 的 V3 源码格式）作为跟踪对象，`.eprj2` 当工作文件。

---

# 附：AI 深度参与 EDA 的失败面清单

> 第二轮调研（2026-09-24）。问的不是“能做什么”，而是“卡在哪”。
> 可信度分三档：`官方明说` / `网友实证` / `没查到`。

## A. API 覆盖：面很宽，成熟面极窄

**核心链路全部标着 BETA**：建工程、建板、自动布局、自动布线、设计规则、DRC、导出 Gerber/BOM/坐标。
BETA 的官方定义原文：“它的任何功能都可能在接下来的开发进程中被修改，**请不要将它用于任何正式环境**。”

少数**非 BETA** 的：`PCB_Document.importChanges`（转 PCB / 导入变更）。

**API 里根本没有的东西**（全量类清单里找不到）：

- **撤销 / 重做 / 事务 / 历史** → 一个都没有，只剩编辑器 UI 的 Ctrl+Z
- **工程级导出 / 另存为 / 版本** → `DMT_Project` 只有 create / open / getInfo / move
- **协作冲突控制、权限粒度** → 无
- **布线算法本体** → 只能导入外部已算好的布线文件（BETA），不能自己实现算法再灌入

## B. 环境与权限

- **外部交互权限**：不开，`SYS_ClientUrl.request` 永远 `throw Error`。
- **CORS 双墙**（官方原文）：“需要在被请求的站点上允许跨源资源共享（CORS），否则接口将始终返回错误结果。”
  → 由此推论：**在扩展里直连 OpenAI / DeepSeek 的 API 会被 CORS 拦死**，只能绕本地桥或自建中继。（CORS 机制是高置信推论，官方没有逐字点名具体厂商）
- **沙箱**（官方原文）：“扩展运行在主线程中，对 **DOM、外部请求、本地文件系统**等浏览器 API 的调用受到限制。”`SYS_FileSystem` 全 BETA。
- 扩展能把编辑器搞坏到需要“灭霸模式”：`?safetyMode=true` 全局禁用扩展。
- **“AI 操作时用户能不能同时动手” → 没查到，空白。**

## C. DRC 与审查

- DRC 查的是**几何 + 已定义规则**，结构上查不出：**引脚顺序颠倒、过孔载流不足、丝印覆盖焊盘、封装与引脚编号脱钩、器件选型是否正确**。这几类只能人工。
- `PCB_Drc.check` 的 `strict` 参数官方注释：“当前 PCB **统一为严格检查模式**”，没有宽松档。
- **DRC 过 ≠ 能打样**：嘉立创免费打样要求线宽 ≥6mil、间距 ≥6mil。
- 一条定位判断（网友实证）：**AI 查错比 AI 画板更靠谱**，底层是结构化比对，擅长引脚悬空/电源域冲突/封装不一致；特征是**高召回、低准确**，**必须人工复核**。

## D. 写回与并发（最要紧，也是最大的空白）

- 官方只公开 **`.epro`（V3 逆行 JSON）** 规范；**`.eprj2` 的结构官方从未公开**。
  （附带：`.eprj2` = SQLite 这一条**我已亲自验证**，文件头就是 `SQLite format 3`；但官方文档里查不到。）
- V3 格式是**最终一致性（CRDT 式）**设计：每行两个 JSON 用 `||` 拼，外层靠 `type/id/ticket` 定序，冲突比 `ticket`，再比文档头 `client`。
  → **外部程序想写回，必须自己实现这套逻辑时钟与冲突裁决**，否则一写就覆盖或判废。
- **没有工程级 `save()`**：`SCH_Document.save()` / `PCB_Document.save()` 存的是“文档”，不是工程文件。
- **没有事务、没有锁、没有回滚 API**。
- **并发**：官方协作只支持“不同文档”并发（靠 CRDT）；**同一文档无锁**，靠“别同时编辑”的纪律。网友实证：“不能多人同时编辑同一个文档，否则保存会相互覆盖。”
  → AI 与人同时落在同一文档，风险与“两个人同时编辑”同级。
- **外部程序写回工程：全网无先例。** 搜过的词：`eprj2 解析`、`嘉立创EDA 工程 外部修改 覆盖 回滚`。

## E. AI 具体停在哪一步（带链接的案例）

| 案例 | 停在哪 |
|---|---|
| B站《让 codex GPT-6 控制嘉立创EDA PCB布线，结果翻车了》[BV16BeB68Epj](https://www.bilibili.com/video/BV16BeB68Epj/) | 原话：“原理图审核很强，pcb 布局也可以”，**布线翻车** ← 最贴我们的问题 |
| B站《GPT-6 Astra 生成的 PCB，到底还有哪些技术上的不足？》[BV1QGhJ69E3h](https://www.bilibili.com/video/BV1QGhJ69E3h/) | 走线规范、DRC 检查、**量产工程**层面有问题 |
| B站《AI 画电路到底靠不靠谱？立创 EDA 插件深度实测》[BV1ffJ56oEWk](https://www.bilibili.com/video/BV1ffJ56oEWk/) | 实测出现**连线错乱、布局不合理** |
| 华秋 EDA 邵中尉：实测 GPT-6 PCB 自动布线 | “AI 布线**卡在数据与工具迭代**” |

## F. 流程 → 能不能交给 AI → 卡的类别

| 步骤 | 能交给 AI 吗 | 卡的类别 |
|---|---|---|
| 建工程 / 建板 | 部分 | API 缺口（BETA） |
| 放器件 | 部分 | 模型能力（选型 / 封装匹配） |
| 原理图连线 | 部分，实测会连线错乱 | 模型能力 |
| 转 PCB / 导入变更 | **可以**（少数非 BETA） | — |
| 原理图审核 / 网表查错 | **较强，但必须人工复核** | 必须人工 |
| PCB 布局 | 部分，成绩一般 | 模型能力 + API 缺口 |
| PCB 布线 | **难，实测翻车** | 模型能力（几何 + 多约束） |
| 铺铜 / 设计规则 / DRC 执行 | 可以跑 | API 缺口（BETA） |
| DRC 结果判读 | **不能全交** | 必须人工（载流 / 工艺 / 选型查不出） |
| 导出 Gerber / BOM / 坐标 | 可以 | API 缺口（BETA） |
| 直连大模型 API | **受阻** | 权限 + CORS |
| 写回工程文件 | **不能 / 未验证** | 工程数据未开放（空白） |
| 事务 / 撤销 / 回滚 | **不能** | API 缺口（无此类 API） |
| AI 与人同文档并发 | **不能，会互相覆盖** | 无锁 |

## G. 两条要更正的旧结论

1. 第一轮调研里提到的“ESP32-S3 四层板案例剩 3 个 DRC WARN”——第二轮**没能找到出处**。
   它只出现在第二轮的转述里，**不要再当证据用**。（这正是“转述会失真”的实例。）
2. 但“`.eprj2` 是 SQLite”这条我已**亲自验证**，可以用。
