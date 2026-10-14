# BMI088 陀螺仪修复与 USB 实板验证

日期：2026-10-12。版本：本地 Betaflight **4.5.2**，目标
`Microair_H743_BMI088`，板名 `MICOAIR743V2_BMI088`。

## 结论和边界

修复前，加速度计工作，但陀螺仪一直处于启动校准中；界面上的零值并不意味着
SPI 完全没有数据。修复后，启动校准完成，三轴角速度响应手动转动，
持续运行 236 秒时仍保持约 2 kHz 的调度频率。

验证仅使用 USB 供电，用户确认拆桨、关闭 Configurator；未发送解锁、电机测试、
接收机配置或参数修改命令。**这不是飞行验证，也不证明电机顺序、安装方向、
失控保护或整机 PID 配置已适合飞行。**

## 原因与修复

1. **陀螺仪默认带宽不适合本板启动校准。** 原驱动把 `NORMAL` 映射为
   532 Hz，实板静止时原始标准差约 48～67 个计数，超过保留的默认校准阈值 48。
   现在 `NORMAL` 使用 230 Hz，仍输出 2 kHz；校准标准差降到约 7～12 个计数，
   `cyclesRemaining` 归零，`CALIB` 消失。`OPTION_1` 改为 532 Hz；它是明确选择
   的高带宽模式，使用前必须重新评估噪声，不能通过关闭校准来强行启用。
2. **慢速陀螺仪触发调度器周期计数器回绕。** 原频率估计窗口固定为 25,000
   次中断；2 kHz 时长 12.5 秒，超过 H743 / 480 MHz 的 32 位周期计数器
   回绕周期约 8.95 秒，导致调度器把 2 kHz 误算为约 6.9 kHz。
   现在按 `INT32_MAX / periodCycles` 限制计数窗口，保留半个计数范围的余量。
   常用 8 kHz 的窗口不变；2 kHz 时约为 8,947 次中断。
3. **SPI 读取加强与诊断。** 陀螺仪命令和六个数据字节改用单个连续事务，
   不给陀螺仪加上加速度计的额外 dummy byte；增加该帧格式的回归测试。
   CLI `gyroregisters` 新增 BMI088 寄存器、原始值、EXTI 计数和校准统计。

连续读取本身、以及把陀螺仪 SPI 降到约 1 MHz 的对照，均未消除校准失败，
因此不将它们误称为主要原因。最终恢复原 SPI 时钟上限 10 MHz 和分频 16。
硬件带宽调整能解决本板问题；本次没有进一步证明 532 Hz 模式高噪声的芯片内部原因。

带宽、量程和读协议依据 [Bosch BMI088 数据手册](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf)。
`BANDWIDTH` 的 bit 7 为只读常 1，因此正常 230 Hz 配置回读为 `0x81`。

## 编译和主机测试

编译方法及参数说明见 [README.md](README.md)。使用 xPack ARM GCC 10.3.1，
BMI088 与原 BMI270 目标均编译成功；原 BMI270 `config.h` 保留不变。

通过的主机测试：

| 测试组 | 项数 | 主要覆盖 |
| --- | ---: | --- |
| BMI088，INT3 | 22 | 双片选、识别、读帧、初始化、量程、带宽、DRDY、轮询回退 |
| 现有陀螺仪 | 5 | 检测、初始化、读取、校准、更新 |
| 方向 | 13 | 现有方向转换及本板 BMI088 映射 |
| 调度器 | 16 | 原测试及 480 MHz / 2 kHz 跨回绕的 15 秒模拟 |
| BMI088，INT4 独立宏配置 | 22 | 备选中断映射，未改变实际 INT3 配置 |

主配置共 **56 项**，另通过 **22 项 INT4** 测试。`git diff --check` 通过。
Windows 诊断脚本还实测拒绝缺少 DFU UID、错误 UID 和覆盖已有备份这三种操作；
这些负向检查的预期结果是错误退出，未执行重启或文件覆盖。
模拟测试不能替代真实 SPI 电气、振动和飞行测试。

## 设备身份、备份和刷写

- Windows VCP：`COM14`；每次刷写前均核验 MCU UID。
- MCU UID：`48-00-46-00-04-51-34-34-36-35-36-34`。
- ROM DFU：单个设备 `USB1`，序列号 `200364500000`。
- 修复前完整 2 MiB Flash：仓库根目录下
  `obj/bmi088-diagnostics/before-fix-flash.bin`。
- 修复前身份、传感器数据、`diff all` / `dump all`：
  `obj/bmi088-diagnostics/before-fix.json`。
- 刷写使用 STM32CubeProgrammer 写入并校验 HEX，结果为
  `Download verified successfully`，然后从 `0x08000000` 启动。
- 没有完整擦除：保留配置扇区 1。修复前后 `resource`、`get gyro`、`get acc`
  完全一致，`diff all` 的参数正文一致；没有放宽 `gyro_calib_noise_limit = 48`。

最终 HEX：`obj/betaflight_4.5.2_STM32H743_Microair_H743_BMI088.hex`。
另保留相同内容的刷写快照：`obj/bmi088-diagnostics/verified-firmware.hex`。
本次刷写文件的 SHA-256：

```text
93a777d84dbdae2c55b028e41382344c84abcabda7a1e03790e693ebd013025f
```

旧 Flash 备份的 SHA-256：

```text
db41c41eea84041c8129dbd7ff811a6972ec6d7d4671bfbed0e56e1650380002
```

`obj/` 被 Git 忽略，这些文件仅在本地保留，请自行复制备份；后续编译会更新默认
HEX 文件，恢复旧 Flash 会连同旧参数一起恢复，必须重新确认设备身份。

## 实板采样记录

全部记录位于仓库根目录的 `obj/bmi088-diagnostics/`。

| 记录 | 验证结果 |
| --- | --- |
| `before-fix.json` | 原固件陀螺仪输出为零，校准未完成 |
| `continuous-spi.json` | 连续事务仍校准失败；约 6.9 kHz 错误调度 |
| `rate-window-stationary.json` | 约 1.99 kHz，8% CPU；仍有 `CALIB` |
| `slow-spi-stationary.json` | 降低 SPI 频率未改善静止原始噪声 |
| `lpf230-stationary.json` | 带宽 `0x81`，校准完成，原始标准差约 8 / 9 / 10 |
| `fixed-motion.json` | 260 个样本、31.1 秒，手动转动时三轴均有正负响应 |
| `fixed-final-stationary.json` | 运行 236 秒仍约 502 µs、8% CPU；`CALIB` 消失 |
| `fixed-cold-boot.json` | 用户断电重接后 140 个样本；校准完成，45 秒时仍约 502 µs、8% CPU |

最后 50 个静止样本的三轴角速度均值约为 -0.032 / 0.037 / 0.005 °/s，
标准差约为 0.218 / 0.139 / 0.246 °/s。加速度 Z 约 2730 个计数，
默认 ±12 g 时约为 1 g。
用户拔下 USB 等待约 5 秒并重新连接后，再次完成识别和校准：
原始校准标准差约 7 / 9 / 12，EXTI 计数为 91,133，`CALIB` 未出现，
资源和参数仍与修复前一致。该轮脚本 JSON 带有 `schemaVersion = 2`。

**MSP 单位说明：** 此 4.5.2 源码的 `gyroRateDps()` 实际返回
`gyroADCf / sensorScale`，因此 MSP 102 的陀螺仪字段是滤波后的传感器计数，
不是直接的 °/s。早期诊断 JSON（包括 `fixed-motion.json`）的 `gyroDps` 字段
名字有误，应按计数解读，乘 `2000 / 32768` 才是 BMI088 的 °/s。
修正后的脚本同时输出 `gyroMspCounts` 和真正换算的 `gyroDps`；
`fixed-final-stationary.json` 已使用修正后的单位。没有改动固件的 MSP 协议。

运动记录按正确比例换算后的范围约为：横滚 -84～84 °/s，
俯仰 -470～218 °/s，偏航 -131～115 °/s。这只能证明动作有响应，
不能证明三轴安装方向和角速度绝对精度均已标定正确。

## 诊断脚本用法与参数

在 Windows PowerShell 中，从仓库根目录运行；WSL 情况下可以调用
`powershell.exe` 并把脚本/输出路径替换成对应的 `\\wsl.localhost\发行版\...` 路径。
先在设备管理器确认实际 COM 号，并断开 Configurator。

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File .\User\configs\Microair_H743_BMI088\tools\probe-bmi088.ps1 `
  -Port COM14 -Samples 30 -IntervalMs 100 -CliBackup `
  -ExpectedUid '48-00-46-00-04-51-34-34-36-35-36-34' `
  -OutputPath "$PWD\obj\bmi088-diagnostics\new-check.json"
```

| 参数 | 含义 |
| --- | --- |
| `-NoProfile` | 不加载用户 PowerShell 配置，减少本机配置干扰。 |
| `-NonInteractive` | 不等待终端交互输入。 |
| `-ExecutionPolicy Bypass` | 仅为此 PowerShell 进程允许执行脚本，不永久修改系统策略。 |
| `-File` | 要执行的 `.ps1` 路径。 |
| `-Port` | 必需，Windows 串口名；不能假定其他电脑也是 COM14。 |
| `-Samples` | MSP IMU 采样次数，1～500，默认 30。 |
| `-IntervalMs` | 两次采样间额外等待毫秒数，20～2000，默认 100；实际还包含通信时间。 |
| `-GyroRegisterSamples` | 额外采集 0～200 次 CLI 原始值/寄存器快照，默认 0；需进入 CLI。 |
| `-CliBackup` | 读取诊断命令及 `diff all` / `dump all`；退出 CLI 会重启飞控。 |
| `-ExpectedUid` | 必须与所选设备完全一致，否则在采样/重启前拒绝继续。 |
| `-OutputPath` | 可选 JSON 输出文件；必须是不存在的新文件，脚本拒绝覆盖。 |
| `-RebootDfu` | 显式请求进入 ROM DFU，不执行刷写；要求准确 UID 和本目标，不能与 CLI 读取组合。 |

脚本不发送 `set`、`save`、电机或解锁指令；检测到已解锁设备会拒绝采样。
`-RebootDfu` 后 USB 断开提示不等于刷写成功，必须重新枚举 DFU 并核对 UID。
不要将本记录中的 UID、COM 号或 DFU 序列号照搬到另一块飞控。
