# 桌面墨水屏信息站 — 输入交互架构图

```mermaid
flowchart LR
    subgraph ENCODER["EC11 旋转编码器"]
        EC11_A["引脚 A
正交信号A"]
        EC11_B["引脚 B
正交信号B"]
        EC11_SW["引脚 按键
按压开关"]
        EC11_COM["引脚 C
公共端 → GND"]
    end

    subgraph Circuit["每通道电路 (以A通道为例)"]
        VCC33["3.3V"]
        R["10kΩ
同一颗电阻
= 上拉 + RC滤波的R"]
        C["0.1μF → GND
RC滤波的C"]
        NODE["公共节点
接 GPIO + EC11 + 电容"]
    end

    subgraph ESP32["ESP32-C3"]
        GPIO_A["GPIO 9
正交A 输入
边沿中断"]
        GPIO_B["GPIO 10
正交B 输入
边沿中断"]
        GPIO_SW["GPIO 0
按键 输入
下降沿中断
⚠️ 需外部10kΩ上拉"]
        PCNT["脉冲计数器
(PCNT单元)
可选硬件计数"]
    end

    VCC33 --> R --> NODE
    NODE --> C
    NODE --> GPIO_A
    EC11_A --> NODE
    EC11_B --> GPIO_B
    EC11_SW --> GPIO_SW
    EC11_COM --> GND

    GPIO_A --> PCNT
    GPIO_B --> PCNT

    style ENCODER fill:#4a90d9,color:#fff
    style Circuit fill:#e6b422,color:#333
    style ESP32 fill:#3a7bd5,color:#fff
```

## 接线表

| EC11 引脚 | 信号 | 中间元件 | → ESP32 GPIO |
|-----------|------|---------|-------------|
| A | 正交A | 10kΩ 上拉到 3.3V + 0.1μF 到 GND | GPIO 9 |
| B | 正交B | 10kΩ 上拉到 3.3V + 0.1μF 到 GND | GPIO 10 |
| 按键一端 | 按压 | 10kΩ 上拉到 3.3V + 0.1μF 到 GND | GPIO 0 ⚠️ |
| 按键另一端 | GND | 直连 | GND |
| C (公共) | GND | 直连 | GND |

⚠️ GPIO 0 是 strapping 引脚，启动时拉低进入下载模式。**必须**加外部 10kΩ 上拉到 3.3V，确保常态高电平。

## 每个引脚的单通道完整电路

一颗电阻·两件事：

```
    3.3V
      │
    10kΩ  ←─ 这一颗就是全部：
      │       ① 把GPIO拉到3.3V（上拉）
      ├──────→ GPIO 9   ② 和电容组成低通滤波（消抖）
      │
     ╱
     ╲ EC11 引脚 A ── 旋转时触点到GND
     ╱
      │
    0.1μF ←── 电容对高频毛刺短路到GND
      │
     GND
```

三个通道（A、B、按键）各一套完全相同的电路，互不共用。

**编码器按键**：EC11 内置按键的一端接这个电路，另一端直接 GND。按压时 GPIO 读到低电平。

## 消抖原理

EC11 是机械触点，旋转时金属弹片会弹跳几十微秒到几毫秒，产生毛刺信号：

```
理想信号:  ████    ████    ████
实际信号:  ██▄▄████▄██▄██████▄▄████▄██▄██
```

RC 滤波器的作用：
- 10kΩ + 0.1μF → 时间常数 τ = 1ms
- 毛刺（μs 级）被电容吸收
- 稳定的电平变化（ms 级）正常通过

## 编码器读取逻辑

```
正转: A 先变低 → B 变低 → A 变高 → B 变高
反转: B 先变低 → A 变低 → B 变高 → A 变高

ESP32 PCNT 硬件自动计数 → 不用软件轮询
→ 旋转时触发中断 → 计数变化 → 更新菜单选项
→ 深度休眠时编码器中断可唤醒 CPU
```

## 按键功能设计

| 按键 | 功能 | GPIO | 备注 |
|------|------|------|------|
| 编码器按压 | 确认/进入 | GPIO 0 | 短按确认，长按回主页 |
