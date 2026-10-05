# Windows IDD 显示驱动（USB 扩展屏）

## 文件

- `xfz1986_usb_graphic_250224_rc_sign.exe` —— **已签名**的 Windows 间接显示驱动（IDD）
  安装包。双击运行，按提示安装即可，**不需要开启测试模式**。

## 它是干什么的

Windows 本身没有能理解本设备自定义 Vendor 协议的通用驱动。这个驱动程序向
Windows 注册一个"虚拟显示适配器"，Windows 的桌面合成器就会把画面渲染给它；
它再把画面编码成 JPEG，通过 USB Bulk 端点推给 ESP32-S31-Korvo-1 板子显示出来。

## 安装

1. 先给板子烧录 `uext_screen` 固件，并用**高速 USB 口**接到 PC（串口日志应出现 `USB Mount`）。
2. 双击 `xfz1986_usb_graphic_250224_rc_sign.exe`，按提示完成安装。
3. 打开**设备管理器**，**显示适配器**下应出现新设备。
4. 打开**设置 → 系统 → 显示**，应能看到一块 800×480 的新显示器。

若没有自动装上，见仓库根目录 `README.md` 的
[「4. 在 Windows 上注册显示驱动」](../README.md#4-在-windows-上注册显示驱动)
一节里的**手动安装**步骤。

## 匹配条件

驱动 INF 按硬件 ID 匹配设备：

- **复合设备**（显示 + 触摸 + 音频）：`USB\VID_303A&PID_2986`
- 纯显示设备（关闭触摸/音频时）：`USB\VID_303A&PID_2987`

因此不要随意修改固件里的 `CONFIG_TUSB_VID` / `CONFIG_TUSB_PID`。

## 来源

驱动源自开源项目
[chuanjinpang/win10_idd_xfz1986_usb_graphic_driver_display](https://github.com/chuanjinpang/win10_idd_xfz1986_usb_graphic_driver_display)。
本目录内附带的是乐鑫官方分发的已签名编译产物。

仅支持 Windows 10 / 11。
