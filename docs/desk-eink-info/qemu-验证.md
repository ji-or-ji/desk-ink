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

## 四、配方（已跑通，2026-09-18）

两个前提：

1. **QEMU 缺 `libiconv-2.dll`**（本机装包不全），需把它所在的目录加进 PATH。本机可用
   `C:\Program Files\Hanako\resources\git\mingw64\bin`（任何带 MinGW `libiconv-2.dll` 的目录都行）。
2. **QEMU 不模拟 ADC 自校准和墨水屏 SPI**，会死循环。用两个开关跳过：
   `CONFIG_APP_SKIP_INPUT` / `CONFIG_APP_SKIP_EINK`（见 `components/app/Kconfig`）。

```powershell
# 1) 环境
. 'C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1'
$env:PATH = 'C:\Program Files\Hanako\resources\git\mingw64\bin;C:\Espressif\tools\qemu-riscv32\esp_develop_9.2.2_20250817\qemu\bin;' + $env:PATH
Set-Location '<工作目录>\desk-ink\software'

# 2) 独立 build 目录 + 独立 sdkconfig（关键：不要把 console/skip 覆盖写进主 sdkconfig）
idf.py -B build-qemu -D SDKCONFIG=sdkconfig.qemu `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu.defaults" build

# 3) 合并 flash（idf.py qemu 会自动做，这里手动以便自己控制运行）
esptool --chip=esp32c3 merge-bin --output=build-qemu/qemu_flash.bin --pad-to-size=4MB `
  --flash-mode dio --flash-freq 80m --flash-size 4MB `
  0x0 build-qemu/bootloader/bootloader.bin `
  0x8000 build-qemu/partition_table/partition-table.bin `
  0x10000 build-qemu/desk-ink.bin

# 4) 跑（QEMU 会阻塞，外部限时/手动退出）
qemu-system-riscv32 -M esp32c3 `
  -drive file=build-qemu/qemu_flash.bin,if=mtd,format=raw `
  -global driver=timer.esp32c3.timg,property=wdt_disable,value=true `
  -nic user,model=open_eth -nographic
```

跑起来后，固件里 `app.c` 的 `dump_fb_ascii(100, 30)` 会把 framebuffer 降采样成
ASCII 打到日志，那就是**真代码渲染出来的画面**。

> 注意：`idf.py qemu` 会阻塞在仿真里，需要在外部限时或手动退出。

---

## 五、拿到精确画面：FB64 + 状态巡视

`dump_fb_ascii` 是 100×30 的粗采样，够看版面、不够看小字。现在有精确方案：

| 开关（默认 n） | 作用 |
|---|---|
| `CONFIG_APP_QEMU_DUMP_B64` | 把 framebuffer（15000 B）base64 打到串口，格式 `FB64 BEGIN 15000 <label>` … `FB64 END` |
| `CONFIG_APP_QEMU_TOUR` | 开机自动渲染全部 UI 状态，每步 dump 一次（`select` 上面的 dump） |

```powershell
# 构建（带巡视开关，注意独立 SDKCONFIG）
idf.py -B build-qemu -D SDKCONFIG=sdkconfig.qemu `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu.defaults" build
# 合并 flash 后跑 QEMU，把串口日志存成文件，再还原 PNG：
pwsh -File tools\fb64-decode.ps1 -Log <日志文件> -OutDir .\fb64 -Scale 2
```

巡视产出 **17 帧**（IDLE 一言/新闻/小窗插画、全屏插画三种信息条、空池、
MENU、CONFIG 六项、TODO_NAV/EDIT、POMO）。样例见 `docs/desk-eink-info/qemu-tour/`。
两个开关默认关闭，正常构建行为不变。

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

## 七、本机实测：QEMU 已端到端跑通（2026-09-18）

固件在 QEMU 里跑到 `app started`，`dump_fb_ascii` 出图，`hello` / `cfg` 都发出：

```
I main: desk-ink booting
E esp_littlefs: Corrupted dir pair at {0x0, 0x1}
W esp_littlefs: mount failed, (-84). formatting...
I store: LittleFS mounted: 8192/2555904 bytes used
I store: NVS ready (namespace 'deskink')
I font: loaded 4037 CJK glyphs + 96 ASCII glyphs
I app: image pool: 0 file(s)
I link: UART0 ready: TX=21 RX=20 115200 8N1
{"t":"hello","fw":"0.1.0"}
{"t":"cfg","refresh":1,"pomo_focus":25,"pomo_break":5,"rotate":1,"art_mode":0,"bar_mode":0}
I app: framebuffer preview (100x30):
I app: .....#...###...#..#...........
...
I app: app started (state=IDLE)
```

### 踩过的四个坑

| 现象 | 原因 | 解法 |
|---|---|---|
| 秒退，退出码 `0xC0000135` | **确实缺 `libiconv-2.dll`**（`llvm-objdump -p` 可见导入；`bin\` 里没有） | 把含该 DLL 的目录（本机 `C:\Program Files\Hanako\resources\git\mingw64\bin`）加进 PATH |
| 卡在 `eFuse: calibration ...` 之后 | `adc_oneshot_new_unit()` 的 ADC 自校准 QEMU 不模拟，死循环 | `CONFIG_APP_SKIP_INPUT=y` |
| 卡在 `font: loaded ...` 之后 | 墨水屏 SPI QEMU 不模拟 | `CONFIG_APP_SKIP_EINK=y` |
| 主构建被 console/skip 覆盖污染 | 两个 build 共用根 `sdkconfig` | 加 `-D SDKCONFIG=sdkconfig.qemu`，别改根 sdkconfig |

> 关于 `--version`：本机 `qemu-system-riscv32.exe --version` 在补齐 `libiconv-2.dll`
> 前**确实返回 `0xC0000135`**，补齐后正常。所以「静态链接、不缺库」的说法在本机不成立。

### 顺带发现的真 bug（已修）

第一次跑到渲染时出现 `font: missing glyph U+FF0C`。
根因：`build_font.ps1` 只取 GB2312 **1~2 区**，而全角 `，！？：；（）` 在 **3 区**。
已改为取 **1~3 区**，字库 3943 → **4037 字 / 134 KB**，缺字消失；相关文档已同步。
