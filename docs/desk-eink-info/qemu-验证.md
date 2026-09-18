# 无硬件验证：QEMU 跑固件

> 更新：2026-09-17
> 状态：**配方已就绪，端到端未跑通**（差最后一步，见第四节）

---

## 一、为什么会有这份

某次汇报说「本机无 C 编译器/QEMU，插画与信息条的实际像素效果需硬件点亮确认」。

**这个前提不成立。** 本机两样都有：

```
C:\Espressif\tools\
  riscv32-esp-elf\esp-15.2.0_20251204\...\riscv32-esp-elf-gcc.exe   ← 交叉编译器
  qemu-riscv32\esp_develop_9.2.2_20250817\qemu\bin\qemu-system-riscv32.exe  ← QEMU
  riscv32-esp-elf-gdb / esp-clang / xtensa-esp-elf / openocd-esp32 ...
```

`idf.py build` **已验证通过**（`desk-ink.bin` 0x63ba0 字节，app 分区余 74%）。

所以"编译不了"不成立；"跑不起来"也不成立，因为 QEMU 在。

---

## 二、QEMU 能跑到哪

`idf.py qemu` 可以启动，能走到：

```
ESP-ROM:esp32c3-api1-20210207
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
load:0x3fcd5830,len:0x1594
load:0x403cbf10,len:0xc9c
entry 0x403cbf1a          ← 二级 bootloader 入口
```

**然后就没有输出了。**

---

## 三、根因：日志走了 USB Serial/JTAG，QEMU 不听

| 项 | 值 |
|---|---|
| 固件配置 | `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`（UART0 让给 VG6328A 蓝牙） |
| QEMU 行为 | 只把 **UART0** 接到 stdio |

所以 ROM 的消息（走 UART0）看得见，IDF 的日志（走 USB-Serial-JTAG）一句也看不见。

**这不是固件的问题，是仿真环境的接线问题。**

---

## 四、配方（下一步要跑的）

给 QEMU 单独一份 console 覆盖：`software/sdkconfig.qemu.defaults`

```powershell
# 1) 载入 IDF 环境
. 'C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1'
$env:PATH = 'C:\Espressif\tools\qemu-riscv32\esp_develop_9.2.2_20250817\qemu\bin;' + $env:PATH
Set-Location '<工作目录>\desk-ink\software'

# 2) 独立 build 目录，套上 console 覆盖（不动主构建）
idf.py -B build-qemu -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu.defaults" build

# 3) 跑
idf.py -B build-qemu qemu
```

跑起来后，固件里 `app.c` 的 `dump_fb_ascii(100, 30)` 会把 framebuffer 降采样成
ASCII 打到日志，那就是**真代码渲染出来的画面**。

> 注意：`idf.py qemu` 会阻塞在仿真里，需要在外部限时或手动退出。

---

## 五、拿到 ASCII 之后

`dump_fb_ascii` 是 100×30 的粗采样，够看版面、不够看小字。两条路：

1. **先用它**：把 ASCII 转回 PNG，验证布局与信息条位置
2. **要精度**：让固件额外支持「把 framebuffer 以 hex 打到串口」，主机端还原成精确位图

第 2 条需要动固件，属于 `变更-显示侧.md` 的延伸。

---

## 六、一个便宜的替代（已经可用）

即使 QEMU 这条线完全不走，像素效果也不是不可验证：

**`tools/render-preview.ps1`** 用同一套数学（缩放/有序抖动/1-bit 打包）
和**设备端的真实字库**，在主机上出图。

区别要说清楚：

| 方式 | 验证了什么 | 代价 |
|---|---|---|
| `tools/render-preview.ps1` | 版面、算法、字模、信息条三模式 | 它是**复刻**，不是真代码 |
| QEMU + `dump_fb_ascii` | **真 C 代码**的渲染结果 | 需要把上面的配方跑通 |

---

## 七、本机实测：QEMU 能跑，但 console 覆盖一开始没生效（2026-09-18 更正）

### 先前结论有误

2026-09-17 曾记录「QEMU 跑不起来、缺 DLL」，**该结论不成立**。

复现结果：`qemu-system-riscv32.exe` 的 `--version` 和完整仿真**都正常**；
`bin\` 里没有 DLL 是**正常的**（可执行文件静态链接，不依赖外部运行库）。

> 判据很硬：若真缺运行库，`--version` 也会失败。它不失败。

### 真正的原因

`build-qemu` 里 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` 仍为 1 —— 第四节给的
`-D SDKCONFIG_DEFAULTS=...` **没有生效**。

原因：`SDKCONFIG_DEFAULTS` 只在**生成新 sdkconfig** 时起作用。项目根的
`sdkconfig` 早已存在，其中显式设置压过了 defaults。

### 正确做法

改**根 `sdkconfig`**（它是生成物、已被 gitignore），而不是塞 defaults：

```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=n
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
```

改完重建，`build-qemu/config/sdkconfig.h` 里应出现 `CONFIG_ESP_CONSOLE_UART_DEFAULT`。
**不做实验时记得改回 USB Serial/JTAG。**

### 验证结果（已复现）

固件在 QEMU 里正常启动：

```
boot: ESP-IDF v6.0.1 2nd stage bootloader
boot: Loaded app from partition at offset 0x10000
app_init: Project name:     desk-ink
spi_flash: detected chip: gd
```

目前在 `spi_flash` 之后停住，`app_main` 的日志尚未出现 —— **下一步查这里**。
