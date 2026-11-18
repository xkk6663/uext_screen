# ESP-IDF v6.1.1 下 ESP32-S31 上电首启 CPU lockup

> 适用：ESP32-S31（ESP32-S31-Korvo-1），ESP-IDF **v6.1.1**。
> v6.1.0 构建的固件不会出现该问题。

## 现象

烧录后用 v6.1.1 构建的固件，**每次上电**都会先复位一次，串口形态固定：

```
ESP-ROM:esp32s31-20251218
rst:0x1 (POWERON),boot:0x78 (SPI_FAST_FLASH_BOOT)
I (26) boot: ESP-IDF v6.1.1 2nd stage bootloader
...
I (164) boot: Loaded app from partition at offset 0x10000
ESP-ROM:esp32s31-20251218
rst:0x1a (CPU_LOCKUP_RESET),boot:0x78 (SPI_FAST_FLASH_BOOT)
Core0 Saved PC:0x500022f6
========== CPU LockUp Exception Report ==========
[Core0]
  1st Exception:
    PCAddr: 0x50004834
--- 0x50004834: esp_ocode_calib_init at components/esp_hw_support/port/esp32s31/ocode_init.c:24
    Cause:  0x00000001
  2nd Exception:
    PCAddr: 0x500022f6
--- 0x500022f6: panic_handler at components/esp_system/port/panic_handler.c:139
    Cause:  0x00000001
```

随后自动再启动一次就正常了，屏、触摸、UAC 全部可用 —— 也就是「每次上电多崩一次、少一次冷启动就能好」。
`Cause: 0x1` 在 RISC-V 上是**指令访问异常（instruction access fault）**。

## 根因

v6.1.1 给 esp32s31 新增了 `components/esp_hw_support/port/esp32s31/ocode_init.c`，
并在 `pmu_init()` 里这样调用（`port/esp32s31/pmu_init.c:238`）：

```c
#if !CONFIG_IDF_ENV_FPGA
    if (esp_rom_get_reset_reason(0) == RESET_REASON_CHIP_POWER_ON) {
        esp_ocode_calib_init();      /* 只在上电首启执行 */
    }
#endif
```

链接结果（`build/uext_screen.map` 实测）：

| 符号 | 段 | 地址 |
| --- | --- | --- |
| `pmu_init` | `.iram0.text` | `0x2f008048` |
| `esp_ocode_calib_init` | `.flash.text` | `0x50004834` |

即 **IRAM 中的早期代码调用了 flash 映射区里的函数**，上电首启那一刻取指失败 →
double exception → `CPU_LOCKUP_RESET`；重启后 `reset_reason` 不再是 `CHIP_POWER_ON`，
校准被跳过，于是"自愈"。

两个旁证：

- IDF **v6.1.0** 没有 `port/esp32s31/ocode_init.c`（只有 c5/c6/c61），所以旧版本不触发；
- 同族 `port/esp32c61/ocode_init.c` 里有 `static void IRAM_ATTR NOINLINE_ATTR calibrate_ocode(void)`，
  说明这类早期校准本就必须放 IRAM，S31 这份漏了属性。

## 修复

`patches/esp-idf-6.1.1-esp32s31-ocode-iram.patch`：给 `set_ocode_by_efuse` 与
`esp_ocode_calib_init` 加 `IRAM_ATTR`，并补 `#include "esp_attr.h"`。

```diff
+#include "esp_attr.h"
-static void set_ocode_by_efuse(int ocode_scheme_ver)
+static void IRAM_ATTR set_ocode_by_efuse(int ocode_scheme_ver)
-void esp_ocode_calib_init(void)
+void IRAM_ATTR esp_ocode_calib_init(void)
```

应用到 IDF 源码树（`IDF_PATH` 根目录）：

```bash
cd "$IDF_PATH"            # 例：/home/esp/esp/esp-idf-6.1.1 或 D:/esp/v6.1/esp-idf
git apply -p1 --check patches/esp-idf-6.1.1-esp32s31-ocode-iram.patch   # 先干跑
git apply -p1         patches/esp-idf-6.1.1-esp32s31-ocode-iram.patch
```

然后 **必须重新编译**（改的是 IDF 组件，不是本工程源码）：

```bash
cd <工程目录> && idf.py fullclean && idf.py build flash
```

回滚：

```bash
cd "$IDF_PATH" && git checkout -- components/esp_hw_support/port/esp32s31/ocode_init.c
```

## 验证

- `git apply -p1 --check` 通过；应用后的文件与实机使用的 IDF 源码逐字节一致。
- 重新编译烧录后，上电串口只有一条 `rst:0x1 (POWERON)`，**不再出现 `rst:0x1a`**，
  `CPU LockUp` 计数 0；随后正常输出：

```
I (405) app_init: Project name:     uext_screen
I (927) S31-Korvo: RGB LCD initialized: 800x480 RGB565, fb_count=2
I (967) gt1151: IC version: GT1158_000101(Patch)_0102(Mask)_00(SensorID)
I (968) S31-Korvo: GT1151 touch initialized
I (951) usbd_uac: UAC Device Start, Version: 1.3.1
```

- 该补丁对性能无影响（只把两个早期函数放进 IRAM，增加几十字节 IRAM 占用）。

## 如果不想改 IDF

保持用 **v6.1.0** 构建（该版本没有这段 S31 ocode 校准），或用 `idf.py` 之外的手段固定工具链版本；
本仓库不提供绕过补丁的其它改法，因为触发点在 IDF 的 `pmu_init()` 里，应用层无法拦截。

## 实测环境

| 项 | 值 |
| --- | --- |
| 芯片 | ESP32-S31 rev v0.0，MAC `30:ed:a0:f4:58:8c`，16 MB flash |
| IDF | v6.1.1（WSL2 Ubuntu-24.04，`/home/esp/esp/esp-idf-6.1.1`） |
| 构建 | `idf 6.1.1 build` → `uext_screen.bin` 416,912 B |
| 烧录 | `idf 6.1.1 -p /dev/ttyUSB0 -b 460800 flash`，3 段 `Hash of data verified` |
