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
