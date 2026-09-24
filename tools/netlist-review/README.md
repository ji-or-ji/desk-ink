# tools/netlist-review：让AI审原理图（导出/执行/比对）

这套脚本是从`docs/desk-eink-info/ai-eda-experiment/`的AI+EDA实验里挑出来的**可复用**部分：下次改完原理图、再跑一轮审查时会用到的都在这里；只服务于那一次实验的探查脚本留在原目录（见文末）。

链路：AI（opencode等编码agent）→`easyeda-api`Skill→本地Bridge Server（Node）→Run API Gateway扩展→立创EDA工程。这套脚本管的是“经Bridge遥控编辑器、把网表取出来、把网表结构化”。AI的定位是**审查员，不是绘图员**。

## 脚本

| 脚本 | 作用 |
|---|---|
| `export-netlist.ps1` | 经Bridge在EDA里跑一段代码，把返回的`result`落成文件。主用途是导出网表 |
| `eda-exec.ps1` | 经Bridge跑一段EDA代码，打印并记录原始返回。查引脚、跑DRC、读工程状态都用它 |
| `parse-netlist.mjs` | 解析Protel2网表文本，输出器件表、网络表、单节点网络（<=1 pin） |

两个`.ps1`用了`-SkipHttpErrorCheck`，需要**PowerShell 7（pwsh）**，不要用Windows PowerShell 5.1跑。

## 跑之前（依赖）

- **Node**：Skill的`SKILL.md`写18+，Run API Gateway的README写22 LTS。以扩展为准，实测v22.20.0可用（便携版即可，不必进系统PATH）。
- **Bridge在跑**：在`easyeda-api`Skill目录下`node scripts/bridge-server.mjs`，监听49620-49629。先确认健康：`GET http://127.0.0.1:49620/health`里`edaConnected:true`。
- **Run API Gateway扩展已装**，且在扩展管理器里勾了**允许外部交互**。没勾或没装，`/execute`会返回`No EDA window connected`。
- **立创EDA窗口开着、已登录，并且已经打开目标工程**（建议开副本，别拿正式工程）。API只作用于当前打开的工程。

## 三步流程

### 1. 导出网表

先复制一份工程当靶子（例如`review-target.eprj2`），在编辑器里打开副本。先列出工程与图页，拿到目标页的uuid：

```powershell
.\eda-exec.ps1 -Label list-docs -Code "const p=await eda.dmt_Project.getCurrentProjectInfo(); const pages=await eda.dmt_Schematic.getAllSchematicPagesInfo(); return {project:p?p.friendlyName:null, uuid:p?p.uuid:null, pages:(pages||[]).map(g=>({uuid:g.uuid,name:g.name,parent:g.parentSchematicUuid}))};"
```

打开目标页，等文档切换完成，再取Protel2网表（把`<pageUuid>`换成上一步的uuid）：

```powershell
.\export-netlist.ps1 -OutFile .\netlist-v2-Protel2.txt -Code "await eda.dmt_EditorControl.openDocument('<pageUuid>'); await new Promise(r=>setTimeout(r,1800)); return await eda.sch_Netlist.getNetlist('Protel2');"
```

两个原理图版本各导一份，互为对照（本次实验用的是v2与v1-验证板）。导出的网表是UTF-8文本。

### 2. 审查

把网表连同项目文档（引脚表、供电/架构类md、固件源码）交给AI。方法见`docs/desk-eink-info/ai-eda-experiment/观察报告.md`，要点是**两轮**：

- 第一轮**闭卷**：只报客观错误，别评论设计选择（否则设计意图会被误报淹没）。
- 第二轮**开卷**：给文档与源码，要求每条标注类别（客观错误/与约束冲突/需人工判断）并**引用到具体条目**。

两轮之差（漏报vs误报）才是有效信息。第二轮还能抓出第一轮碰不到的**网络级**和**跨文件耦合**问题。

### 3. 比对

```powershell
node .\parse-netlist.mjs .\netlist-v2-Protel2.txt
node .\parse-netlist.mjs .\netlist-v1验证板-Protel2.txt
```

输出每颗器件、每个网络的节点数、以及单节点网络。用它人工复核AI的结论（比如“某网络只挂了一个脚”是不是真的）。

## 已知的坑

1. **编码**：Bridge返回的正文实测是UTF-8，但历史上遇到过部分中文错误消息是GBK/GB18030字节，一律按UTF-8解会变成`U+FFFD`乱码。两个`.ps1`现在是**严格UTF-8优先、遇非法字节再回退GB18030**，两类来源都能解出。**不要图省事一律用GB18030解**，那会把正常UTF-8的中文解成乱码（例如“错误”会变成“閿欒”）。如果你发现新的中文乱码，先按这个思路定位：把响应原始字节dump出来，分别按UTF-8与GB18030各解一遍再判断。

2. **`searchByProperties`在HALF_OFFLINE环境被禁**：半离线（非全离线）客户端调这个接口会直接`HTTP 500`，中文错误为“非全离线客户端环境无法调用本接口！”。想按属性搜器件走不通，改用`lib_Device.search`或`lib_Device.getByLcscIds`。

3. **`modify`偶发报错且会清空值**：改`otherProperty`时偶发`Cannot destructure property 'cmdKey' of 'i' as it is undefined.`（`HTTP 500`），并且**触发那次会把目标值清成空串**。改完**务必回读确认**，不要只看`modified:true`就当成功。

4. **其他几个坑**（本次实验实测，供参考）：
   - `sch_Netlist.getNetlist("JLCEDA")`会30s超时，只用`"Protel2"`。
   - `lib_Footprint.get`会抛`[object Object]`，拿不到封装焊盘数（所以“符号脚数vs封装焊盘数”只能标“需人工判断”）。
   - `modify`只改内存不落盘，要`eda.sch_Document.save()`才写盘；没有工程级save，也没有undo/transaction/rollback。
   - 外部程序直接写`.eprj2`能写进去，但会被编辑器下一次保存**静默覆盖**（无锁、无冲突检测）。所以这条路别走。

## 目录归属

本目录只放“下次再审还用得上”的脚本。以下**一次性**脚本仍留在`docs/desk-eink-info/ai-eda-experiment/`：

- `inspect-eprj2.mjs`、`inspect2.mjs`：探`.eprj2`（SQLite）的表结构与`history_data`内容，是“搞清工程格式”这一步的产物。
- `probe-datastr.mjs`：探`history_data.dataStr`的编码，一次性。
- `external-write.mjs`、`check-structure.mjs`：外部直改`.eprj2`那组对照实验（写入+检查板名是否被覆盖），一次性。
- `probe-bridge.ps1`：Bridge的启动与探测脚本，硬编码了便携Node与Skill的绝对路径，而且**末尾会把刚起的Bridge杀掉**，别在审查过程中跑。
