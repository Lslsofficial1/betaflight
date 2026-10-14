# MicoAir H743 V2：BMI088 配置与验证

这是本地 Betaflight **4.5.2** 的 BMI088 适配，不是官方原版 4.5.2 已有的驱动。
原 BMI270 配置 `User/configs/Microair_H743/config.h` 保留不变；编译 BMI088 时选择
`CONFIG=Microair_H743_BMI088`，不要继续选择 `Microair_H743`。

## 1. 硬件连接和配置

| 信号 | STM32 引脚 | 新 `config.h` 定义 |
| --- | --- | --- |
| SPI2 SCK | PD3 | `SPI2_SCK_PIN` |
| SPI2 MISO，接传感器 SDO | PC2 | `SPI2_SDI_PIN` |
| SPI2 MOSI，接传感器 SDI | PC3 | `SPI2_SDO_PIN` |
| 陀螺仪 CS | PD5 | `GYRO_1_CS_PIN` |
| 加速度计 CS | PD4 | `GYRO_1_ACC_CS_PIN` |
| 陀螺仪 INT3 / DRDY | PC15 | `GYRO_1_EXTI_PIN` |

`SDI`/`SDO` 必须注意参照的是哪一端：上述 Betaflight 引脚宏指 STM32 端，
因此 STM32 SDI/MISO 接 BMI088 SDO，STM32 SDO/MOSI 接 BMI088 SDI。

用户已确认 PC15 接陀螺仪 **INT3**，因此 `BMI088_GYRO_INT_PIN` 设置为 `3`。
若将来硬件接到 INT4，应改成 `4` 并重新编译；仅修改 STM32 的 EXTI 引脚不足以
改变 BMI088 内部中断映射。加速度计独立的中断引脚未用于本驱动。

其他外设沿用原配置，MAX7456 仍在 SPI1；不再配置原 BMI270 使用的 SPI3。

### 安装方向与晶振

新配置使用 `SYSTEM_HSE_MHZ 8` 和 `GYRO_1_ALIGN CW270_DEG_FLIP`。
依据是 [ArduPilot 的 MicoAir743v2 硬件定义](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_HAL_ChibiOS/hwdef/MicoAir743v2/hwdef.dat)：
该板采用 8 MHz 晶振，BMI088 的方向定义为 `ROTATION_ROLL_180_YAW_270`。
对照 [ArduPilot 的向量旋转实现](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_Math/vector3.cpp)
与本仓库 `src/main/sensors/boardalignment.c`，两者均对应 `(x, y, z) -> (-y, -x, -z)`。
这是根据公开板级定义得到的配置，**不是实板验证结果**。若使用改版 PCB、外接模块
或不同安装方向，必须按实际安装修改；不能仅凭 SPI 引脚确定方向。

## 2. 驱动行为

- 陀螺仪与加速度计共用 SPI2，但使用独立设备描述符和片选；同时核验两颗芯片 ID。
- SPI 使用模式 3，配置时钟不超过 10 MHz。
- 加速度计在上电/复位后先用一次片选操作激活 SPI；读取时跳过额外 dummy byte。
  陀螺仪读取不插入这个额外 dummy byte。
- 陀螺仪输出 2 kHz、量程 ±2000 °/s；加速度计输出 800 Hz、默认量程 ±12 g，
  `acc_high_fsr` 开启时为 ±24 g。两个传感器不启用 Bosch 的硬件数据同步功能。
- 正常硬件低通对应 **230 Hz** 带宽；`gyro_hardware_lpf = OPTION_1` 对应 532 Hz。
  这两种配置均保持 2 kHz 输出，不支持用此驱动获得 8 kHz / 32 kHz 输出。
  这是对早期本地适配中两种映射的修正；本板静止实测 532 Hz 下原始噪声会
  阻止默认阈值的启动校准，建议保持 `NORMAL`，不要关闭校准检查。
- INT3 为高电平有效、推挽输出，PC15 使用上升沿 EXTI。中断只记录时间戳和计数，
  调度器据此同步；SPI 读取仍在任务内执行，不在中断里执行。
- 首次陀螺仪读取时，若启动期间未收到至少 5 个 DRDY 边沿，则回退为 2 kHz
  定时读取。回退不是断线自检，也不意味着传感器或中断接线通过了实测。
- 这两个设备的 SPI DMA 禁用，避免将栈上的缓冲区通过 DMA 访问 H7 DTCM。
- 陀螺仪命令和六个轴数据字节使用一个连续 SPI 事务，丢弃命令阶段返回值。
- 调度器按照 CPU 时钟和陀螺仪周期限制频率测量窗口，避免 H743 的 32 位
  周期计数器在 2 kHz、25,000 次中断的旧测量窗口内回绕、误算约 7 kHz。
- 新增 `resource ACC_CS 1`；新增字段的陀螺仪设备参数组版本升级，避免按旧布局
  读取已有配置。未启用 BMI088 的目标继续使用原参数布局和版本。

## 3. 编译固件

在仓库根目录执行。下面使用已经安装的 xPack GCC 10.3.1；若目录不同，请替换
`ARM_TOOLCHAIN_BIN`，它应指向含 `arm-none-eabi-gcc` 的 **bin 目录**。

```bash
ARM_TOOLCHAIN_BIN="$HOME/opt/toolchains/xpack-arm-none-eabi-gcc-10.3.1-2.3/bin"
"$ARM_TOOLCHAIN_BIN/arm-none-eabi-gcc" --version

env PATH="$ARM_TOOLCHAIN_BIN:$PATH" make -j4 \
  CONFIG=Microair_H743_BMI088 CONFIG_DIR=User \
  ARM_SDK_PREFIX="$ARM_TOOLCHAIN_BIN/arm-none-eabi-" CCACHE= \
  obj/betaflight_4.5.2_STM32H743_Microair_H743_BMI088.hex
```

| 参数 | 含义 |
| --- | --- |
| `ARM_TOOLCHAIN_BIN` | 当前 shell 中的工具链路径变量，不是 Make 参数。 |
| `env PATH=...` | 给这一条构建命令提供工具链搜索路径。 |
| `-j4` | 最多同时运行 4 个构建任务，可按内存/CPU 调整。 |
| `CONFIG=Microair_H743_BMI088` | 选择新的 BMI088 板级配置。 |
| `CONFIG_DIR=User` | 从 `User/configs/<CONFIG>/config.h` 读取配置。 |
| `ARM_SDK_PREFIX=.../arm-none-eabi-` | 指定 GCC、链接器、objcopy 等工具的绝对路径前缀，不是某一个可执行文件名。 |
| `CCACHE=` | 禁用 ccache，避免依赖本机是否安装它。 |
| 最后一项 `.hex` 路径 | 直接指定 HEX 构建目标，并约束本次构建并行度。 |

产物是 `obj/betaflight_4.5.2_STM32H743_Microair_H743_BMI088.hex`。
用于调试和检查符号的 ELF 位于 `obj/main/betaflight_STM32H743_Microair_H743_BMI088.elf`。
`obj/` 被忽略不影响这些产物在本地生成和保留。

原 BMI270 配置的回归编译命令，仅更换 `CONFIG` 和目标文件名：

```bash
env PATH="$ARM_TOOLCHAIN_BIN:$PATH" make -j4 \
  CONFIG=Microair_H743 CONFIG_DIR=User \
  ARM_SDK_PREFIX="$ARM_TOOLCHAIN_BIN/arm-none-eabi-" CCACHE= \
  obj/betaflight_4.5.2_STM32H743_Microair_H743.hex
```

## 4. 主机单元测试

在仓库根目录执行，使用本机 Clang/Clang++，而不是 ARM GCC：

```bash
make -C src/test -j4 \
  test_accgyro_bmi088_unittest test_sensor_gyro_unittest \
  test_alignsensor_unittest test_scheduler_unittest \
  CC=clang CXX=clang++ LDFLAGS=-Wl,-T,unit/pg.ld \
  OBJECT_DIR=../../obj/test-bmi088-repair-lpf230
```

`-C src/test` 先切换到测试目录；四个 `test_...` 目标编译并运行对应测试。
`CC`/`CXX` 覆盖 C/C++ 编译器；`LDFLAGS` 保留参数组所需链接脚本；
`OBJECT_DIR` 将本次测试产物与旧测试缓存隔离。若改动测试后出现 GCDA 覆盖率缓存
不兼容提示，可换一个新的输出目录重跑，不需要清空全部固件产物。
本机没有 `libBlocksRuntime`，而这四组测试没有使用 Clang Blocks，故使用上述
定向覆盖，未修改全局测试链接配置。其他测试是否需要 Blocks 应分别确认。

BMI088 的 22 项测试覆盖双片选识别、错误 ID/无效资源、SPI 激活与 dummy byte、
复位/上电序列、量程与采样率、正负轴数据与温度、总线忙、DRDY 寄存器映射、
EXTI 时间戳、连续轴数据读取以及缺少中断时的回退。另运行 5 项陀螺仪测试、
13 项方向测试和 16 项调度器测试，共 **56 项**。调度器新增测试模拟
480 MHz、2 kHz 的 15 秒运行，覆盖周期计数器回绕及旧测量窗口。
测试使用模拟 SPI/GPIO/EXTI，
不能验证实际电平、焊接、电气时序、噪声或飞行稳定性。

还可使用独立输出目录，验证 INT4 的备选驱动分支，不改变实际板级 INT3 配置：

```bash
make -C src/test -j4 test_accgyro_bmi088_unittest \
  CC=clang CXX=clang++ LDFLAGS=-Wl,-T,unit/pg.ld \
  OBJECT_DIR=../../obj/test-bmi088-repair-lpf230-int4 \
  'accgyro_bmi088_unittest_DEFINES=USE_ACCGYRO_BMI088= BMI088_GYRO_INT_PIN=4 SPI_IO_CS_CFG=0'
```

`OBJECT_DIR` 隔离不同编译宏的目标文件；最后一项覆盖此测试的宏定义列表。
`SPI_IO_CS_CFG=0` 是主机 GPIO 模拟配置，**不是飞控实际 GPIO 配置**。

## 5. 实板验证：编译通过不等于可飞

1. 拆下螺旋桨。刷写前保存旧固件及 CLI 的 `diff all` / `dump all`。
2. 使用新的 BMI088 HEX。更新本地同一 BMI088 目标时，默认保留参数扇区，不做
   完整擦除；本次修复已保留并核对原有资源、陀螺仪和加速度计参数。
   从不同目标切换时应先备份，再按实际目标重新配置；不要整体导入旧 BMI270 的
   资源、传感器方向和硬件设置。只有明确需要重建配置时才完整擦除。
3. 启动后用 CLI 查询：

   ```text
   version
   status
   resource
   get gyro_1_sensor_align
   get acc_hardware
   gyroregisters
   tasks
   ```

   确认目标为 BMI088 版本，陀螺仪/加速度计识别为 BMI088，而非出现 `NOGYRO`。
   资源应包含 `GYRO_CS 1 D05`、`ACC_CS 1 D04`、`GYRO_EXTI 1 C15`，
   默认方向为 `CW270FLIP`；检查陀螺仪任务周期约 500 µs（2 kHz）。
4. 用示波器/逻辑分析仪确认 PC15 有约 2 kHz 的上升沿，SPI2 的两个 CS 独立动作。
   即使没有 DRDY，轮询回退也可能让数据继续更新，所以仅凭界面有数据不能证明
   中断正常。`gyroregisters` 可显示中断计数、原始三轴值、SPI 分频以及校准统计。
   正常配置下 `BANDWIDTH` 为 `0x81`，`calibration remaining` 应最终为 `0`，
   `status` 的 `CALIB` 标志应消失；`BANDWIDTH` 的 bit 7 是只读位，不是错误。
5. 校准加速度计，检查模型俯仰、横滚、偏航都与实际动作一致；静止时加速度
   接近 1 g。然后检查传感器曲线、温度、任务负载和黑匣子噪声记录。
6. 重新核对电机序号/转向、接收机、失控保护、电压电流及 OSD；本次保留的旧配置
   不表示这些外设已经做过实板测试。上述项目通过前不要装桨试飞。

寄存器和 SPI 协议依据：[Bosch BMI088 数据手册](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf)。

本次修复的诊断、备份、刷写和 USB 实板验证记录见 [VALIDATION.md](VALIDATION.md)。
