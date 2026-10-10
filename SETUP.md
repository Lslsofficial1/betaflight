# Betaflight 编译与调试环境配置指南

## 目录

- [环境要求](#环境要求)
- [快速开始](#快速开始)
- [编译](#编译)
- [烧录 (DAPLink)](#烧录-daplink)
- [调试 (DAPLink)](#调试-daplink)
- [VS Code 使用](#vs-code-使用)
- [配置复用脚本](#配置复用脚本)
- [常见问题](#常见问题)

---

## 环境要求

| 工具 | 版本要求 | 用途 | 检查命令 |
|------|---------|------|---------|
| arm-none-eabi-gcc | 13.3.1 | ARM 交叉编译 | `arm-none-eabi-gcc --version` |
| Git Bash | ≥ 2.40 | POSIX shell (make 必需) | `git --version` |
| Python | ≥ 3.8 | 构建脚本依赖 | `python --version` |
| OpenOCD | 0.12.0+ | DAPLink 烧录/调试 (可选) | `openocd --version` |
| VS Code | 最新版 | IDE | - |
| STM32CubeIDE Extension | 推荐 | `stgdbtarget` 调试类型 | VS Code 扩展市场搜索 |

### 关键约束

**Makefile 依赖 POSIX 环境（`uname`）**，无法直接从 PowerShell 调用。必须通过 Git Bash 包装：

```bash
# 在 Git Bash 中运行
export PATH="/d/tool_chain/arm_gcc_13.1.1/bin:$PATH"
cd /d/A_Project/betaflight
make STM32F405
```

---

## 快速开始

### 1. 运行配置脚本（推荐）

```powershell
# 默认参数（适用于当前项目）
.\setup-betaflight.ps1

# 自定义参数
.\setup-betaflight.ps1 -TargetMCU "STM32F405" -BoardName "OMNIBUSF4" -AdapterSpeed 4000
```

脚本会自动完成：
- 创建 `mk/local.mk` 指向工具链
- 生成 `.vscode/` 全部配置文件
- 更新 `.gitignore`
- 可选：初始化 git submodule（加 `-InitSubmodules`）

### 2. 手动配置

如果需要手动配置，参考 [配置复用脚本](#配置复用脚本) 章节。

---

## 编译

### VS Code 方式

1. 打开项目，按 `Ctrl+Shift+B`（默认构建）
2. 或 `Ctrl+Shift+P` → `Tasks: Run Task` 选择：
   - `make STM32F405` — 编译 STM32F405 平台
   - `make OMNIBUSF4` — 编译 OMNIBUSF4 板级配置
   - `make clean` — 清理构建产物

### 命令行方式

```bash
# Git Bash 中
cd /d/A_Project/betaflight
export PATH="/d/tool_chain/arm_gcc_13.1.1/bin:$PATH"

# 编译平台目标
make STM32F405

# 编译具体板级配置（F405 板）
make OMNIBUSF4

# 查看可用目标
make help | grep F405
```

### 编译输出

| 文件 | 路径 |
|------|------|
| ELF | `obj/main/betaflight_STM32F405.elf` |
| HEX | `obj/betaflight_2026.12.0-alpha_STM32F405.hex` |

编译信息示例：
```
Linking STM32F405
Memory region         Used Size  Region Size  %age Used
           FLASH:         392 B        16 KB      2.39%
          FLASH1:      622404 B       992 KB     61.27%
             RAM:      106328 B       128 KB     81.12%
```

---

## 烧录 (DAPLink)

### VS Code 任务

`Ctrl+Shift+P` → `Tasks: Run Task`：
- `openocd-flash STM32F405` — 烧录平台固件
- `openocd-flash OMNIBUSF4` — 烧录板级配置固件
- `openocd-erase STM32F4` — 全片擦除

### 命令行

```bash
openocd.exe -s D:/OpenOCD-0.12.0/share/openocd/scripts \
    -f interface/cmsis-dap.cfg \
    -c "adapter speed 4000" \
    -f target/stm32f4x.cfg \
    -c "program obj/main/betaflight_STM32F405.elf verify reset exit"
```

---

## 调试 (DAPLink)

### 前置条件

1. DAPLink 通过 USB 连接到电脑
2. DAPLink 的 SWD 引脚连接到 STM32F405 的 SWDIO/SWCLK/GND
3. 已通过 `make` 生成了 ELF 文件

### VS Code 调试步骤

1. `F5` 或 `调试` 面板 → 选择 **DAPLink OpenOCD - STM32F405**
2. 调试器将自动启动 OpenOCD 连接 DAPLink
3. 设置断点后按 `F5` 继续

### 调试面板说明

| 功能 | 快捷键 |
|------|--------|
| 设置断点 | 点击行号左侧 |
| 继续 | F5 |
| 单步跳过 | F10 |
| 单步进入 | F11 |
| 停止调试 | Shift+F5 |

---

## VS Code 使用

### 项目文件结构

```
betaflight/
├── mk/
│   └── local.mk              # 工具链路径配置
├── .vscode/
│   ├── c_cpp_properties.json # IntelliSense 配置
│   ├── tasks.json            # 构建/烧录任务
│   ├── launch.json           # 调试配置
│   └── settings.json         # 项目设置
├── setup-betaflight.ps1      # 一键配置脚本
├── Makefile                  # 编译入口
└── SETUP.md                  # 本文件
```

### IntelliSense 配置

`.vscode/c_cpp_properties.json` 已配置：
- 工具链路径和 MCU 宏定义
- 所有 STM32F4 标准外设库头文件路径
- CMSIS/Core 和 CMSIS/DSP 路径
- VCP/USB 驱动路径

### 搜索排除

`.vscode/settings.json` 配置了构建产物和工具链的搜索排除，避免 IntelliSense 和搜索干扰。

---

## 配置复用脚本

`setup-betaflight.ps1` 可用于其他 STM32 项目，参数化配置：

```powershell
# 语法
.\setup-betaflight.ps1 [-ProjectPath <路径>] [-ToolchainPath <路径>]
    [-OpenOCDPath <路径>] [-GitBashPath <路径>]
    [-TargetMCU <型号>] [-BoardName <板名>]
    [-AdapterSpeed <kHz>] [-OpenOCDTargetCfg <cfg文件名>]
    [-InitSubmodules]

# 示例：配置 STM32F411 项目
.\setup-betaflight.ps1 -ProjectPath "E:\Other\betaflight" `
    -TargetMCU "STM32F411" -BoardName "CRAZYBEEF4DX" `
    -OpenOCDTargetCfg "stm32f4x.cfg"

# 示例：配置 STM32G0 项目（如 FireBox_Test 模式）
.\setup-betaflight.ps1 -ProjectPath "E:\Other\g0" `
    -TargetMCU "STM32G030" -OpenOCDTargetCfg "stm32g0x.cfg" `
    -AdapterSpeed 1000
```

### 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-ProjectPath` | 当前目录 | 项目根目录 |
| `-ToolchainPath` | `D:\tool_chain\arm_gcc_13.1.1` | GCC 工具链路径 |
| `-OpenOCDPath` | `D:\OpenOCD-0.12.0` | OpenOCD 安装路径 |
| `-GitBashPath` | `D:\Git\bin\sh.exe` | Git Bash sh.exe 路径 |
| `-TargetMCU` | `STM32F405` | MCU 型号 |
| `-BoardName` | `OMNIBUSF4` | 板级 CONFIG 名称 |
| `-AdapterSpeed` | `4000` | DAPLink SWD 速度 (kHz) |
| `-OpenOCDTargetCfg` | `stm32f4x.cfg` | OpenOCD 目标板配置文件 |
| `-InitSubmodules` | 不启用 | 执行后自动初始化 submodule |

---

## 常见问题

### Q1: `failed to detect operating system` 错误

makefile 通过 `uname` 检测 OS，PowerShell 没有此命令。**必须通过 Git Bash 调用**：

```bash
# 错误：PowerShell 直接调用
mingw32-make -C D:/A_Project/betaflight STM32F405

# 正确：通过 Git Bash 包装
D:/Git/bin/sh.exe -lc "export PATH='/d/tool_chain/arm_gcc_13.1.1/bin:$PATH'; cd /d/A_Project/betaflight && make STM32F405"
```

VS Code 的 tasks.json 已自动用 Git Bash 包装，`Ctrl+Shift+B` 直接使用即可。

### Q2: GCC 版本不匹配错误

betaflight 要求 GCC 13.3.1，版本检查严格。通过 `mk/local.mk` 指定工具链路径：

```makefile
ARM_SDK_PREFIX := D:/tool_chain/arm_gcc_13.1.1/bin/arm-none-eabi-
```

### Q3: OpenOCD 连接 DAPLink 失败

1. 确认 DAPLink 已正确连接（设备管理器中可见）
2. 确认 SWD 引脚接线正确：
   - DAPLink SWDIO → STM32F405 PA13
   - DAPLink SWCLK → STM32F405 PA14
   - DAPLink GND → STM32F405 GND
   - DAPLink 3.3V → STM32F405 3.3V（可选，取决于板设计）
3. 降低 `AdapterSpeed` 值重试（如 1000）

### Q4: 找不到板级 CONFIG

```bash
# 查看 F405 相关的可用配置
cd /d/A_Project/betaflight
export PATH="/d/tool_chain/arm_gcc_13.1.1/bin:$PATH"
make help | grep F405

# 或查看所有可用配置
make help
```

### Q5: 编译速度太慢

```bash
# 启用并行编译
make -j8 STM32F405

# 增量编译（只编译修改的文件）
make STM32F405
```

### Q6: 全片擦除

```bash
openocd.exe -s D:/OpenOCD-0.12.0/share/openocd/scripts \
    -f interface/cmsis-dap.cfg \
    -c "adapter speed 4000" \
    -f target/stm32f4x.cfg \
    -c "init; reset halt; stm32f4x mass_erase 0; reset run; shutdown"
```

---

## 架构速查

```
[VS Code]              [Git Bash]            [OpenOCD]          [DAPLink]        [STM32F405]
    │                      │                    │                   │                │
    │  Ctrl+Shift+B        │                    │                   │                │
    │──────────────────────>│ make STM32F405     │                   │                │
    │                      │<───────────────────┘                   │                │
    │                      │  ELF 输出                          │                │
    │                      │                                    │                │
    │  openocd-flash       │                                    │                │
    │─────────────────────>│ openocd.exe -f cmsis-dap.cfg ─────>│───SWD───>      │
    │                      │                    │                   │  FLASH/W   │
    │                      │                                    │                │
    │  F5 (Debug)          │                                    │                │
    │─────────────────────>│ openocd GDB server ───────────────>│───SWD───>      │
    │                      │                    │                   │  GDB R/W   │
    │<─────────────────────┘                    │                   │                │
    │  GDB 交互 (断点/变量/单步)                                                        │
```

---

*最后更新: 2026-08-26*