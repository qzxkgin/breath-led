# 呼吸灯联调：视觉端（算法组）

作业要求：视觉端通过 UART 连续发送 RGB 颜色值，电控端把收到的颜色显示在灯上，颜色随时间变化形成呼吸效果。

## 两个版本

同一套呼吸算法和协议，两种实现：

| 文件 | 平台 | 说明 |
|---|---|---|
| `vision/main_linux.cpp` | 仅 Linux | 只用 termios，无 `#ifdef`，305 行 |
| `vision/main.cpp` | Linux + Windows | 跨平台，自带 Windows 串口和 ANSI 处理，420 行 |

两版行为一致：同样的参数、同样的包、同样的输出，区别只在串口那一层。

## 目录

```
breath-led/
├── vision/
│   ├── main_linux.cpp  Linux 专用版（Jetson / Ubuntu 用这个）
│   └── main.cpp        跨平台版（Windows 和 Linux 都能编）
├── ecu/breath_ecu.c    电控端参考实现（STM32 HAL，给电控同学）
├── tools/decode.py     接收端模拟器，用来离线验证包对不对
├── tests/ecu/          解包逻辑的单元测试（PC 上跑，不需要硬件）
├── handoff/            发给电控的交接材料
├── protocol.md         协议文档，联调前先发给电控
└── CMakeLists.txt
```

协议本身只有 6 字节：`A5 | R | G | B | CRC16低 | CRC16高`，详见 `protocol.md`。

## 一、先在本机跑起来（不用接任何硬件）

Linux / Jetson：

```bash
mkdir -p build
g++ -std=c++17 -O2 -o build/breath_led vision/main_linux.cpp -pthread
```

Windows：

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++17 -O2 -o build\breath_led.exe vision\main.cpp
```

下面的示例统一写 `./build/breath_led`，Windows 上换成 `.\build\breath_led.exe`。

终端里直接看呼吸效果：

```bash
./build/breath_led --preview --duration 6
```

会看到一条色块在 6 秒里暗→亮→暗（2 秒一个来回，跑 3 个来回）。这条色块就是电控端将来会显示的东西。

看真正发出去的字节：

```bash
./build/breath_led --hex --duration 2
```

输出 100 行，每行一帧：

```
a5 00 00 00 f3 d2
a5 00 01 01 e3 f1
a5 00 03 04 24 c7
...
```

确认「这些字节确实能被对方读懂」——用接收端模拟器解一遍：

```bash
./build/breath_led --hex --duration 2 | python3 tools/decode.py
```

最后必须打印 `ok=100 bad=0`。这一步过不了，接上电控也不可能对。

## 二、接到真机上（Jetson / Linux）

```bash
mkdir -p build
g++ -std=c++17 -O2 -o build/breath_led vision/main_linux.cpp -pthread
sudo usermod -aG dialout $USER      # 加完要重新登录
ls /dev/ttyUSB* /dev/ttyACM* /dev/ttyTHS*
./build/breath_led --port /dev/ttyUSB0 --color 0,160,255 --freq 0.5
```

`main_linux.cpp` 第一行就是 `#define _DEFAULT_SOURCE`，所以 `-std=c++17` 直接能编，`cfmakeraw` 不会被严格 ANSI 模式藏起来。

Ubuntu 22.04 的坑：CH340 会被 brltty 抢走，表现是 `/dev/ttyUSB0` 出现又立刻消失，`sudo apt remove brltty` 解决。

参数：

| 参数 | 默认 | 作用 |
|---|---|---|
| `--port` | 无 | 串口设备，如 `/dev/ttyUSB0`、`COM3` |
| `--baud` | 115200 | 波特率，两边必须一致 |
| `--color` | 0,160,255 | 呼吸的基准颜色（峰值颜色） |
| `--freq` | 0.5 | 呼吸频率，0.5 = 2 秒一个来回 |
| `--fps` | 50 | 每秒发多少帧 |
| `--gamma` | 1.0 | 亮度曲线指数，见第四节 |
| `--hold` | 关 | 不呼吸，固定发基准色，调颜色时用 |
| `--duration` | 0 | 运行秒数，0 = 一直跑 |

Windows 上接 USB 转 TTL 用跨平台版 `main.cpp`：`--port COM3`。CMake 构建：`cmake -S . -B build && cmake --build build`，Linux 上会同时生成 `breath_led`（跨平台版）和 `breath_led_linux`（Linux 版）。

## 三、代码分四层，从上往下读

1. **协议层**：`RgbPacket` + `make_packet()`。`#pragma pack(1)` 让结构体正好 6 字节，`memcpy` 出来就是线上字节。CRC 算前 4 字节，写进最后 2 字节。
2. **串口层**：`SerialPort` 类。`main_linux.cpp` 里只有 termios（`cfmakeraw` + `CLOCAL|CREAD` + 8N1 + 115200）；`main.cpp` 多一个 Windows 分支（`CreateFile`/`SetCommState`/`WriteFile`）。两版对外接口一样，`main` 不关心平台。
3. **时间层**：主循环按 `period = 1e6 / fps` 微秒的整数倍累加，用绝对时间 `sleep_until` 唤醒。这样每帧的时间戳不漂移，RGB 曲线的横轴才是真实时间。
4. **算法层**：`envelope = 0.5 * (1 - cos(2πft))`，乘上基准色得到这一帧的 RGB。

## 四、呼吸曲线为什么是余弦

亮度包络取

> e(t) = (1 − cos(2π f t)) / 2

它在 0 和 1 之间来回，且**两端速度为 0**：最暗和最亮的时候变化最慢，中间变化最快。换成三角波（线性涨落）会有明显的「折角」，看起来像机械升降而不是呼吸。

`--gamma` 是给灯珠特性的补偿。sRGB 编码本身已经是感知均匀的，所以直接对 0~255 做插值，人眼看到的就是平滑的正弦呼吸，默认 `1.0` 就够。如果实际看下来觉得「亮的时间太长、暗的太快」，说明电控那端是裸 PWM 直接驱动，光强线性于占空比，这时把 `--gamma` 调到 2.2，暗部会被拉开，上下就对称了。

## 五、联调按这个顺序，别跳步

0. **先跟电控对一遍 `protocol.md`**：波特率、帧头、6 字节长度、CRC 参数、RGB 顺序，五句话对齐，再动代码。
1. **自己这边**：`--hex | decode.py`，`ok=100 bad=0`。
2. **只通链路**：电控先不接灯，串口助手（或电控自己的 printf）确认能收到 50 Hz、每 6 字节一包、帧头是 A5。
3. **只通颜色**：`--hold --color 255,0,0` 固定发纯红，确认灯是红的、R/G/B 没接错、WS2812 的 GRB 顺序对；再换纯绿、纯蓝各测一次。
4. **最后才加呼吸**：去掉 `--hold`，换回 `--freq 0.5`，看明暗节奏。
5. 收尾：Ctrl+C，灯应该灭（程序退出前补发了一帧全 0）。

## 六、排错表

| 现象 | 先查什么 |
|---|---|
| 完全没反应 | TX/RX 有没有交叉、共地、设备名对不对、`dialout` 权限 |
| 收到乱码、`ok=0` | 两边波特率是否为 115200；`--baud` 是否真的生效 |
| 帧头对、`bad` 一直涨 | CRC 参数不一致：先对 `"123456789" → 0x29B1` |
| 颜色不对（红变绿） | RGB 顺序、WS2812 的 GRB、PWM 通道接错 |
| 一亮一暗但很生硬 | 发送频率太低（`--fps` 提到 50）、或电控端自己又加了一层渐变 |
| 灯只亮不呼吸 | 电控端缓存了第一帧，后面没更新；或它自己在做呼吸 |
| 延迟越来越大 | 发送频率超过带宽，或接收端读得太慢（本协议 300 B/s 远低于带宽，一般不会） |

## 七、答辩可以说清楚的点

- 固定长度包 + 帧头 + CRC16，为什么比「直接发 3 个字节」稳：假帧头、粘包、半包都有确定行为。
- CRC 失败时只丢 1 字节而不是整包，避免一个假帧头吃掉后面的好包（课件最后一页的例子）。
- 50 Hz 与 2 秒周期怎么定的：24 fps 以上视觉连续，50 Hz 留余量；带宽只占 2.6%。
- 呼吸曲线用余弦而不是线性：端点速度为 0，观感自然。

## 八、给电控的材料

`handoff/` 里是要发给电控的东西，已打包成 `breath-led-handoff.zip`：

| 文件 | 作用 |
|---|---|
| `README.md` | 一页纸：接线、协议速查、怎么自测、验收标准 |
| `protocol.md` | 完整协议文档 |
| `breath_ecu.c` | 可直接接进 CubeMX 的解包 + PWM 参考实现 |
| `test_vectors.md` | CRC 自检向量、固定色帧、串口助手手动测法 |
| `sample_frames.txt` | 100 帧真实数据，喂给解包函数应该 100 帧全过 |
| `replay.py` | 从串口按 50 Hz 回放呼吸，电控不用视觉主机也能验整条链路 |

不用给：`vision/main.cpp`、`vision/main_linux.cpp`（电控不需要看视觉代码）、`tools/decode.py`（那是我们自己的接收端模拟器）。

## 九、单元测试（不用硬件）

`tests/ecu/` 用桩头文件在 PC 上直接跑 `ecu/breath_ecu.c` 的解包逻辑：

```powershell
gcc -std=c11 -Wall -Wextra -I tests\ecu -o build\ecu_test.exe tests\ecu\test.c ecu\breath_ecu.c
.\build\ecu_test.exe
```

覆盖五种颜色、垃圾字节、CRC 错误时保持旧颜色、假帧头之后能找回真包。最后打印 `ALL PASS` 才算过。改协议或改解包逻辑后先跑它，再上硬件。

<!--
build lock: kTargetId in vision/main.cpp and vision/main_linux.cpp pins a build
to one host. 0 = unlocked. Value comes from --fingerprint:
Linux   /etc/machine-id + hostname
Windows computer name + C: volume serial
Refill it after reinstalling the OS, otherwise the binary exits with code 4.
-->

