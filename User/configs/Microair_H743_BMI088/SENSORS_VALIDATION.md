# SPL06 / QMC5883L 配置软件验证

日期：2026-10-21。版本：本地 Betaflight 4.5.2；目标：
`Microair_H743_BMI088`，板名：`MICOAIR743V2_BMI088`。

## 修改范围

- 启用 `USE_MAG` 与 `USE_MAG_QMC5883`，把磁力计默认总线从 I2C1 改为 I2C2，
  地址固定为 `0x0D`。
- SPL06 保留现有 `USE_BARO_DPS310` 兼容驱动，把默认 I2C 地址设为 `0x77`；
  原驱动默认地址 `0x76` 不会自动回退探测 `0x77`。
- 两者共用 PB10/PB11；I2C2 默认时钟设为 400 kHz。
- 增加 `BARO_EOC_PIN PD0` 记录板级 DRDY 接线。现有 DPS310 驱动仍按周期读取，
  不启用或依赖 PD0 的 EXTI；未增加加速度计 INT1 中断。
- 增加旧参数升级用的 `enable-baro-mag.cli`，不自动修改已连接设备。
- 未修改气压计/磁力计生产驱动算法、原 BMI270 配置、电机资源或接收机参数。

引脚依据：[微空官方手册](https://micoair.cn/zh/docs/flight-controller/micoair743/micoair743v2-flight-controller-manual)。
SPL06 地址交叉依据：[ArduPilot 同板定义](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_HAL_ChibiOS/hwdef/MicoAir743v2/hwdef.dat)
中的 `BARO SPL06 I2C:0:0x77`。QMC5883L 地址为当前 BF 驱动所用的 `0x0D`。

## 测试结果

编译与测试命令见 [README.md](README.md)。

| 测试 | 项数 | 结果 |
| --- | ---: | --- |
| DPS310 / SPL06 兼容路径 | 11 | 通过 |
| QMC5883L | 9 | 通过 |
| BMI088 | 22 | 通过 |
| 原陀螺仪逻辑 | 5 | 通过 |
| 安装方向 | 13 | 通过 |
| 调度器 | 16 | 通过 |
| 合计 | 76 | 通过 |

新增测试覆盖：明确指定 SPL06 的 `0x77` 地址、保留其他板的 `0x76` 驱动默认行为、
错误 ID/I2C 读取失败、上电就绪位、分段读取补偿系数、连续测量配置、正负原始值和
负系数补偿、总线忙和读取启动失败；QMC5883L 识别、排除悬空总线的全 `0xFF`
响应、200 Hz/±8 gauss 初始化、DRDY 状态轮询、带符号小端数据、溢出解锁和部分
I2C 失败路径。没有模拟全部电气故障，也没有验证气压计 PD0 中断。

QMC5883L 的 9 项测试额外随机顺序运行 30 轮全部通过，检查异步读取状态不会污染
后续测试。模拟 I2C 在函数调用中完成，不模拟真实异步传输耗时或多设备总线争用。

使用实际 ARM 目标头文件与 xPack ARM GCC 10.3.1 完成配置编译断言：
8 个必需宏、2 个禁止选择 SPI 默认值的宏、12 个总线/地址/时钟/引脚断言均通过。
检查最终 ELF 参数重置函数，确认气压计默认总线 2、地址 119、DRDY 资源 D00，
磁力计默认总线 2、地址 13；最终 ELF 保留了 DPS310 和 QMC5883L 的读数回调。

## 固件产物

完整 ARM 构建成功，无编译警告或错误。产物：

```text
obj/betaflight_4.5.2_STM32H743_Microair_H743_BMI088.hex
```

此产物 SHA-256：

```text
0a3e1ce09aecfb0758547f824fd5aa8e17314f0ced14a3413788a5ee0990f5f7
```

SHA 仅对应本次生成的文件，后续重新编译会受构建时间等影响。
旧 BMI088 USB 验证固件 `obj/bmi088-diagnostics/verified-firmware.hex` 未覆盖，
原 BMI270 的 `User/configs/Microair_H743/config.h` 未修改。

## 尚未完成的实板验证

本次没有连接串口、修改飞控参数、刷写固件、执行磁力计校准或飞行测试。
不能把软件测试通过表述为已经在实板识别并验证两颗传感器。

旧 USB 记录中气压计地址为 `0`、I2C2 时钟为 800 kHz，且没有启用磁力计。
保留旧参数升级不会自动采用新的全部默认值；需备份后按 README 导入传感器专用
CLI 文件，再确认检测状态、动态读数、I2C 错误是否持续增长，并校准、核对磁力计
方向后才可使用相关飞行功能。不要通过完整擦除或改动 PG 版本强制重置整机参数。
