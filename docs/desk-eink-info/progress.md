# 桌面墨水屏信息站 — 原理图绘制进度表

## 电源管理 — BQ24073RGTR + XC6206 + MCP3021

- [x] USB-C 接口（VBUS → BQ24073 IN）
- [x] BQ24073 IN → 1μF + 0.1μF → GND
- [x] BQ24073 CE → GND
- [x] BQ24073 EN2 → GND / EN1 → VOUT（USB500 模式）
- [x] BQ24073 ISET → 2kΩ → GND（500mA 充电）
- [x] BQ24073 TMR → 36kΩ → GND（2h 定时）
- [x] BQ24073 TD → GND（启用终止）
- [x] BQ24073 TS → 10kΩ → GND（无 NTC）
- [x] BQ24073 BAT → 电池保护板 P+ / 1μF → GND
- [x] BQ24073 OUT 接三处：
  - 4.7μF → GND（输出电容）
  - XC6206 VIN（LDO 供电）
  - 系统负载（ESP32 VDD 等宽压设备）
- [x] BQ24073 CHG → 1kΩ → LED → VOUT（充电指示）
- [x] BQ24073 EP → GND（热焊盘接好地）
- [x] XC6206 VIN → 1μF → GND
- [x] XC6206 VOUT → 3.3V / 1μF → GND
- [x] 电池 P+ → BQ24073 BAT / P- → GND（保护板自带，无需额外画）
- [x] 分压 100kΩ + 100kΩ（VOUT → 分压中点 → MCP3021 AIN → GND）
- [x] MCP3021 VDD → 3.3V / VSS → GND / SCL/SDA → I2C 总线

## I2C 总线 — GPIO 5 (SDA) + GPIO 6 (SCL)

- [x] SDA 4.7kΩ → 3.3V
- [x] SCL 4.7kΩ → 3.3V
- [x] MCP3021A3T SCL/SDA 挂在总线上
- [x] DS3231MZ+TRL SCL/SDA 挂在总线上

## 时钟 — DS3231MZ+TRL

- [x] VCC → 3.3V
- [x] GND → GND
- [x] SCL/SDA → I2C 总线
- [x] SQW → GPIO 7
- [x] VBAT → 10μF → GND（无纽扣电池）
- [x] 32kHz → 悬空
- [x] RST# → 悬空（内部上拉）

## 墨水屏 — DEPG0420BNS19AF0（24-pin FPC）

- [x] FPC 接插件选型（FPC-05F-24PH20 或兼容）
- [x] Pin 5 VSH2 ── 1μF/25V ── GND（引脚不接地）
- [x] Pin 8 BS1 → GND（低电平 = 4 线 SPI）
- [x] Pin 9 BUSY → GPIO 8
- [x] Pin 10 RES# → GPIO 2
- [x] Pin 11 D/C# → GPIO 3
- [x] Pin 12 CS# → GPIO 4
- [x] Pin 13 SCL → GPIO 18
- [x] Pin 14 SDA → GPIO 19
- [x] Pin 15 VDDIO → 3.3V
- [x] Pin 16 VCI → 3.3V
- [x] Pin 17 VSS → GND
- [x] Pin 18 VDD → 3.3V（电容 1μF/25V ── GND）
- [x] Pin 19 VPP → 悬空
- [x] Pin 20 VSH1 ── 1μF/25V ── GND（引脚不接地）
- [x] Pin 21 VGH ── 1μF/25V ── GND（引脚不接地）
- [x] Pin 22 VSL ── 1μF/25V ── GND（引脚不接地）
- [x] Pin 23 VGL ── 1μF/25V ── GND（引脚不接地）
- [x] Pin 24 VCOM ── 0.47μF/25V ── GND（引脚不接地）

## 蓝牙 — VG6328A

- [x] VDD → 3.3V
- [x] GND → GND
- [x] TXD → GPIO 20
- [x] RXD → GPIO 21
- [x] PA9 → GPIO 1

## 五向导航开关（控制器，2026-09-16 变更）

> ⚠️ 原 SIQ-02FVS3 编码器已弃用，原理图需按下列新接法重画。

- [ ] COM（公共端）→ GPIO 0（100kΩ 上拉到 3.3V）
- [ ] 上 → 1kΩ → GND
- [ ] 下 → 2.7kΩ → GND
- [ ] 左 → 5.6kΩ → GND
- [ ] 右 → 12kΩ → GND
- [ ] 中央 → 22kΩ → GND

## ESP32-C3-WROOM-02-N4 模组

- [x] 3V3 → 3.3V
- [x] EN → 10kΩ → 3.3V + 0.1μF → GND
- [x] GND 脚全部接地
- [x] GPIO 0 确认外部 100kΩ 上拉
- [x] GPIO 1 → VG6328A PA9
- [x] GPIO 2 → EINK RES#
- [x] GPIO 3 → EINK D/C#
- [x] GPIO 4 → EINK CS#
- [x] GPIO 5 → I2C SDA（4.7kΩ 上拉到 3.3V）
- [x] GPIO 6 → I2C SCL（4.7kΩ 上拉到 3.3V）
- [x] GPIO 7 → DS3231 SQW
- [x] GPIO 8 → EINK BUSY
- [ ] GPIO 9 → （待定，用五向开关后释放）
- [ ] GPIO 10 → （待定，用五向开关后释放）
- [ ] GPIO 0 → 五向开关 COM（外部 100kΩ 上拉）
- [x] GPIO 18 → EINK SCL
- [x] GPIO 19 → EINK SDA
- [x] GPIO 20 → VG6328A TXD
- [x] GPIO 21 → VG6328A RXD
