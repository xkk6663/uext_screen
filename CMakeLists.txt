# =============================================================================
# uext_screen —— 独立 USB 扩展屏（副屏）固件
#   目标硬件：ESP32-S31-Korvo-1 开发板（16 MB flash）
#
#   这是一个自包含的独立工程：克隆 → idf.py build → idf.py flash 即可运行。
#   板子通过 USB(High-Speed) 作为「显示器」接入 PC，Windows 安装 IDD 驱动后
#   系统会多出一块 800x480 的扩展显示器（并同时提供触摸与 UAC 声卡）。
#
#   详细原理、上手教程与排障请见 README.md
# =============================================================================
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-ignored-qualifiers)
project(uext_screen)
