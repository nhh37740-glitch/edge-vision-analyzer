# 实测基线与优化收益

本文件记录已核验的魔百盒、联想 PC、手机源、本地 RTSP 模拟源、go2rtc ONVIF／RTSP 模拟源、CAVIAR 公开真实视频、盒端 RTSP 推流输出与 Fluent Bit／journald→PC 日志链路结果；整机重启恢复和长期日志压力仍 **待实测**。记录触发条件以 [AGENTS.md](../AGENTS.md) 为准，项目入口见 [README](../README.md)，指标字段与单位见 [API 契约](api.md)。设计目标、示例配置、硬件称呼和接口示例都不能代替实测；所有尚未实测或核验的值统一写为 **待实测**。

实际记录时同时保留提交、设备与配置、源素材／源协议、负载开关、预热／取样窗口、样本量、连续运行时长和北京时间记录日期，链接对应原始命令输出、PC 日志或采样记录。比较前后保持相同工况，注明采样工具及统计口径；证据先脱敏，不上传私有影像、凭据或本地私有配置。本轮记录 2026-11-24 魔百盒本地视频、手机 IP Webcam 流、盒到 PC 云存储与 LLM 联动，联想侧 MediaMTX + FFmpeg 本地 RTSP 模拟源、go2rtc ONVIF／RTSP 模拟源，以及 CAVIAR 公开真实视频复验与盒端 RTSP 推流输出；手机流在 11:50–12:07 可用，随后手机端服务停止；16:21–16:37 完成硬件补充核验与日志链路基础验收。

## 1. 真实硬件与运行环境

型号与指令集以设备实际输出为准；`lscpu`、device-tree 等为后续采集来源，不据产品名推断。运行库版本以实际加载版本及证据登记，不能只抄构建配置。

| 核验字段 | 魔百盒（边缘运行） | 联想 PC（构建／推理／存储） | 手机源（视频源） |
| --- | --- | --- | --- |
| 精确型号／SoC（lscpu／/proc/device-tree/model 实读） | `SKYWORTH E900V22C`；device-tree 为 `amlogic,g12a`，soc0 报告 `Amlogic Meson`／`G12A (Unknown)`，revision `28:c - 70:2`；具体 S905 商业型号未核验 | `Intel(R) Pentium(R) Gold 6500Y`（Win32_Processor） | 待实测 |
| CPU 架构／核心规格 | aarch64，4 核 Cortex-A53，每核 1 线程（lscpu） | x86_64，2 核 4 线程（Win32_Processor） | 待实测 |
| NEON 指令支持及证据 | 支持；`/proc/cpuinfo` 的 Features 含 `asimd`，不含 `asimddp` | 不适用（x86_64） | 待实测 |
| AVX 指令支持及证据 | 不适用（aarch64） | 待实测（未读取 CPU flags） | 不适用 |
| 内存容量／类型／规格 | `free -b` 总内存 2,029,686,784 字节（1935.66 MiB）；DDR 代际／速率／通道未核验 | 17,096,011,776 字节（约 15.9 GiB，Win32_ComputerSystem）；类型／规格未读取 | 待实测 |
| OS 发行版／版本 | Armbian OS 25.02.0 noble（`/etc/os-release`） | Windows 11 Home 10.0.26200 | 待实测 |
| 内核版本 | 6.6.74-happy-new-year aarch64 | 10.0.26200（Windows NT） | 待实测 |
| glibc 运行版本 | 2.39-0ubuntu8.3（`ldd --version`） | 不适用（Windows） | 不适用 |
| FFmpeg 运行版本／动态库主版本 | FFmpeg 7.1.1 静态链接；libavformat 61.7.100、libavcodec 61.19.101、libavutil 59.39.100、libswscale 8.3.100；RTSP 推流另用 `/usr/local/bin/ffmpeg` 7.0.2-static（libx264 0.164.3191、RTSP muxer） | RTSP 模拟器使用 FFmpeg N-129241（`g05c674c913`）CLI；go2rtc v1.9.15 提供 RTSP 与 ONVIF Device／Media 子集；`llama-server` 不依赖 FFmpeg | 不适用 |
| NCNN 运行版本 | 源码 tag `20261020`，静态编入 | 不适用 | 不适用 |
| llama.cpp／llama-server 运行版本／提交 | 未在边缘运行；验收时临时开启后关闭 | `b12669`（commit `30ec35ae2`），Windows x64 CPU 构建 | 不适用 |
| 采集软件／视频源协议 | edge-vision-analyzer 读取本地 MP4（H.264）并循环；手机流经 PC 中继时读取 HTTP MJPEG；RTSP 模拟源经 PC TCP 中继读取 H.264；go2rtc 1.9.15 用 exec + libx264 循环 30 秒 CAVIAR 素材并提供 ONVIF 子集，盒端经同一 PC 中继读取；CAVIAR 真实视频转 H.264 后用同一 RTSP 路径；`streamer.mode=rtsp` 时再经同一中继向 MediaMTX `/edge` 推 H.264 | 运行 `llama-server :8080` 与 FastAPI 云存储 `:8090`；MediaMTX v1.21.4（RTSP :8554／API :9997，路径 `cam`／`edge`）+ FFmpeg 推流；go2rtc v1.9.15（RTSP :8554、API／ONVIF :1984，均绑定回环） | IP Webcam `/shot.jpg` 200（1920×1080）、`/video` 200；硬件与系统版本待实测 |
| 采集证据／日期／输出记录 | 2026-11-24 11:50–11:53、12:25–12:26、13:22–13:23、14:15–14:20、14:55–14:56（北京时间）；`/api/v1/health`、`/api/v1/stats`、`journalctl`、`systemctl show` | 2026-11-24 12:10–12:11、12:24–12:26、14:02–14:20、14:50–14:56（北京时间）；`/v1/models`、`/health`、存储 `/api/v1/events` 与图片响应、MediaMTX `/v3/paths/list` 与 `ffprobe` 拉流、go2rtc ONVIF SOAP 响应 | 2026-11-24 11:50–12:07（北京时间）；`/shot.jpg` 与 `/video` 响应 |

2026-11-24 16:21（北京时间）追加只读硬件核验，来源为 device-tree、soc0、`lscpu`、`free -b`、`lsblk -b`、cpufreq 与 thermal sysfs，公开记录见 [Discussion #148](https://github.com/nhh37740-glitch/edge-vision-analyzer/discussions/148)。设备树可能来自适配镜像，不据盒名推断商业 SoC 型号。

| 补充硬件项 | 实读值与边界 |
| --- | --- |
| CPU 修订与指令集 | Cortex-A53 r0p4；4 核 4 线程；`fp asimd evtstrm aes pmull sha1 sha2 crc32 cpuid`，未暴露 `asimddp`／`fphp`／`asimdhp` |
| 内置 MMC | `mmcblk2`，名称 Biwin；7,818,182,656 字节（7.28125 GiB）；逻辑／物理块均为 512 字节 |
| zram | 交换设备约 967.83 MiB，另有 50 MiB 挂载 `/var/log`；ramlog enabled，不能据 zram 推断所有日志均无持久化 |
| CPU 调频 | `cpufreq-dt`／`schedutil`；驱动档位 1.0–1.908 GHz；策略 1.512–1.908 GHz；瞬时读数 1.908 GHz，不代表额定频率或持续性能 |
| 温区与阈值 | CPU 43.5°C、DDR 温区 43.9°C；CPU passive/hot/critical 配置为 85/95/110°C，DDR passive/critical 为 85/110°C；阈值未由压测触发，温区名称不证明 DDR 代际 |
| GPU 与冷却 | device-tree 为 `amlogic,meson-g12a-mali`／`arm,mali-bifrost`；具体核心型号与性能未核验；CPU/GPU cooling 采样状态均为 0，非应用温控能力 |

## 2. 实际模型参数、内存与流控

填写实际运行生效值并附来源，不能预填设计默认值。队列需要逐节点核对容量、用途和丢弃行为；进程 RSS、systemd 限制和 journal 文件空间分别记录。

| 参数分组 | 实际核验字段 | 生效值 | 配置／运行证据 |
| --- | --- | --- | --- |
| 端侧检测 | 模型名称／版本 | YOLO-Fastest-v2（`models/yolo-fastest-v2.param/bin`） | `GET /api/v1/config` 与模型文件 |
| 端侧检测 | 输入尺寸 | 320 | 同上 |
| 端侧检测 | 量化／数值精度 | fp32（`auto` 且未配置量化候选，回退基础模型） | 同上；`/proc/cpuinfo` 无 `asimddp` |
| 端侧检测 | 推理线程数 | 2 | `GET /api/v1/config` |
| 端侧检测 | 置信度阈值 | 0.5 | `GET /api/v1/config` |
| 帧差过滤 | MAD 阈值 | 8.0 | `GET /api/v1/config`；PATCH 热更新另测 |
| 多模态推理 | GGUF 模型名称／版本 | Qwen2-VL-2B-Instruct Q4_K_M（`models/Qwen2-VL-2B-Instruct-Q4_K_M.gguf`） | llama-server `/v1/models` 与模型文件 |
| 多模态推理 | GGUF 量化 | Q4_K_M | 同上 |
| 多模态推理 | mmproj 文件／版本 | `mmproj-Qwen2-VL-2B-Instruct-Q8_0.gguf` | llama-server 启动参数与 `/v1/models` 的 `multimodal` 能力 |
| 多模态推理 | 图像 token 上限 | 64（`--image-max-tokens 64`） | llama-server 启动参数；同一快照由 425 token 降至 88 token |
| 事件图像 | JPEG 质量 | 70（事件图像 18,648 字节；实时快照固定 80） | `config.local.json`；PC 存储图片 |
| 事件流控 | 事件触发冷却时间 | 15 秒（仅 1 条持续事件，冷却未触发） | `config.local.json` |
| 采集 | 实际采集分辨率 | 640×480 | `GET /api/v1/config` 与快照文件 |
| 采集 | 实际采集 FPS | 14.9 | `/api/v1/stats` 的 `capture_fps` |
| 有界队列 | CaptureNode 队列容量／用途／丢弃行为 | 2／帧队列／满时丢最旧 | `BaseNode` 构造与 `src/main.cpp` 装配 |
| 有界队列 | MotionNode 输入队列容量／丢弃行为 | 2／满时丢最旧 | 同上 |
| 有界队列 | NcnnDetectorNode 输入队列容量／丢弃行为 | 2／满时丢最旧 | 同上 |
| 有界队列 | EventTrackerNode 输入队列容量／丢弃行为 | 2／满时丢最旧 | 同上 |
| 有界队列 | StreamOutNode 输入队列容量／丢弃行为 | 2／满时丢最旧 | 同上 |
| 有界队列 | GenAiWorker 待处理队列容量／丢弃行为 | 1／只保留最新事件 | `src/transport/genai_worker.cpp` |
| 云存储 | 盒到 PC 真机投递结果 | 1 条事件 `evt_0_1`（person 0.817），中文摘要“女子站在门口，按下门铃。”，LLM 7744 ms，图片 18,648 字节可读取 | PC `/api/v1/events` 与 `/api/v1/images/2026-11-24/evt_0_1.jpg` |
| 服务内存限制 | systemd 实际生效 MemoryMax | 314572800 字节（300 MiB） | `systemctl show edge-vision -p MemoryMax` |
| 内存日志 | journald 实际存储模式／内存文件空间上限 | `Storage=volatile`／`RuntimeMaxUse=20M`／`ForwardToSyslog=no` | `systemd-analyze cat-config`；`/etc/systemd/journald.conf.d/zz-edge-vision.conf` 晚于发行版 `syslog.conf`；空间不是实时硬上限 |
| 日志采集流控 | Fluent Bit 输入缓冲阈值／存储模式 | `Mem_Buf_Limit=2MB`／`storage.type=memory`，无 DB 或磁盘积压 | 已安装的 `fluent-bit.conf`、systemd input 启动日志；阈值不代表 RSS |

### 日志链路验收（2026-11-24）

应用提交为 `cb6131b`，部署配置为本 PR 的 `deploy/` 模板；目标机 noble／arm64，Fluent Bit 5.2.0、libpq5 16.15 离线安装，`--dry-run` 成功，采集器 active／enabled。样本来自 SSH 命令、`/proc/<MainPID>/status`、journal 和 PC API，不公开含机器标识的完整 journal 对象。应用保持本地 30 秒 MP4 循环、640×480@15、MAD 8.0，LLM／存储关闭。

| 验收项 | 实测结果与口径 |
| --- | --- |
| 最终配置资源窗口 | 16:36:36–16:37:36（北京时间），60 秒、每 15 秒 1 次，共 5 次 |
| Fluent Bit RSS | `VmRSS` 21,760–22,016 KiB（21.25–21.50 MiB） |
| 采集器 cgroup | `MemoryCurrent=4,075,520` 字节，5 次均相同；`MemoryMax=100,663,296` 字节（96 MiB），`NRestarts=0`；不与 RSS 混用 |
| journal 文件空间 | 5 次 `journalctl --disk-usage` 均为 `19.9M`，位于 `/run/log/journal`；没有 `/var/log/journal` |
| PC 接收与查询 | HTTP output 返回 204；GET 当日 `tail=1000` 返回 200／`application/x-ndjson`；抽取的 6 条真实 journal 记录均含 `MESSAGE` 和 edge unit，非 edge 为 0；另有此前手工 API 探针不计入 journal 样本 |
| 过滤与 syslog 旁路 | 唯一非 edge `logger` 探针匹配 0；关闭 journal→syslog 转发后，重启 edge 产生的新启动消息在 syslog 新增区间匹配 0，文件 inode 未变；未删除既有日志，不声称整机其他日志都无持久化 |
| 接收端不可达 | 16:34:42–16:35:02 临时指向关闭的 `127.0.0.1:1`，PC 服务保持运行；实际 flush 失败、11 秒后重试、耗尽后丢弃；服务仍 active、`NRestarts=0`、RSS 22,016 KiB、`MemoryCurrent=4,202,496` 字节 |
| 恢复 | 自动恢复 PC 地址并重启采集器，随后重启 edge；16:35:51 新批次返回 204，PC 查询读到新日志。只验证后续投递恢复，不承诺历史补传 |
| 视频侧影响 | 采集 14.9 FPS、检测 0.1 FPS、丢帧 0；应用 RSS 27.4–27.5 MB，SoC 39.4–39.9°C；未单独测日志采集 CPU 增量 |
| 未覆盖 | 整机重启、真实网线断开、长时间高吞吐和 PC 文件留存压力均待实测；不把关闭端口模拟写成物理断网验收 |

### ARM64 发行包构建验收（2026-11-24）

Issue #144／PR #151 的[手动 run 45368104945](https://github.com/nhh37740-glitch/edge-vision-analyzer/actions/runs/45368104945)基于提交 `2e8bfccfdefbc5168ffa22607192f959d3dccad5`，在 17:44–17:59（北京时间）运行成功。联想 Windows／MSYS2 只做 native 验证，ARM64 产物由 Ubuntu 24.04 x86_64 runner 交叉构建；前九行是构建与 PC 侧检查证据，后几行为 2026-11-25 在该产物上的目标机部署与运行结果，两者分别记录。

| 验收项 | 实际结果与边界 |
| --- | --- |
| 联想 native | 构建通过，8 个 C++ CTest 目标全部通过，并行耗时 1.94 秒 |
| Runner native | 构建通过，9 个 CTest 全部通过（8 个 C++ 目标与 `test_deploy_script`），并行耗时 0.92 秒 |
| Runner Python | `py_compile` 成功；锁定依赖环境的 pytest 5 项通过，0.64 秒；有 1 条 Starlette／AnyIO 弃用警告 |
| ARM64 构建与打包 | 交叉编译、安装、递归运行库打包及 artifact 上传步骤均成功；`readelf` 报告 AArch64，程序 RUNPATH 为 `$ORIGIN/../lib` |
| 直接动态依赖 | FFmpeg `libavformat.so.60`、`libavcodec.so.60`、`libavutil.so.58`、`libswscale.so.7`，另有 cpp-httplib 0.14、OpenMP、libstdc++ 等；这是新包的 ELF 依赖，不是盒端旧程序的 FFmpeg 7.1.1 运行版本 |
| Artifact | 名称 `edge-vision-analyzer-arm64`，ID `14908217952`；Actions 外层 ZIP 为 65,247,073 字节，内层 `tar.gz` 为 65,644,399 字节，不等同于解压后运行目录大小 |
| PC 包内容核对 | 常规下载与限时重试超时后，通过 Azure 存储地址的六段 HTTP Range 传输取回并安全解压；实际含 131 个运行库文件、122 个动态依赖包条目及对应版权文件，另含 NCNN、nlohmann/json、stb 许可；此处为 11 月 24 日 PC 检查结果，11 月 25 日部署另列下行 |
| PC ELF 抽检 | 主程序及 5 个直接依赖库（4 个 FFmpeg 库、cpp-httplib）均为 AArch64；主程序 RUNPATH 为 `$ORIGIN/../lib`，抽检库均为 `$ORIGIN`。依赖清单的 FFmpeg 源包版本为 `7:6.1.1-3ubuntu5`；不替代盒端加载器解析与运行验收 |
| 公开发布 | 11 月 24 日手动事件的 `publish-release` 按设计跳过，该次未创建正式 Tag／Release；正式发布见下方 11 月 25 日行 |
| 盒端加载与部署 | 2026-11-25 09:50 恢复直连后，停止 edge／Fluent Bit，安装同包 `lib/`（131 个文件）并替换主程序。目标加载器 `--list` 退出 0，131 个库从 `/opt/edge-vision-analyzer/lib` 解析、4 个来自系统 glibc 目录、`not found` 为 0。既有 `deploy_to_box.sh` 完成对时、保留现场配置、重启与 health 200，日志仅出现动态构建的一条 swscale 加速不可用提示 |
| 盒端运行 | 同一 PID 连续 108 秒、跨 30.13 秒本地视频至少 3 次循环，`NRestarts=0`、丢帧 0、采集 14.9 FPS、`rss_memory_mb` 53.9、`VmRSS` 55,156 kB、SoC 38.4 °C、快照 200／`image/jpeg`／20,702 字节且 PC 可解码。PATCH 临时把 MAD 设为 0 运行 20 秒，API 累计检测 FPS 为 2.1、推理均值 41.3 ms（FPS 包含之前跳过检测的时间，不代表强制检测窗口的瞬时吞吐），随后恢复 8.0 并核对生效值 |
| 退出与恢复 | `systemctl stop` 161 ms 完成，`Result=success`、`ExecMainStatus=0`、MainPID=0、无崩溃栈；重启后 edge 与 Fluent Bit 均 active／running，health 200，stats 正常。清理后再重启，至 10:09:11 连续运行约 5.5 分钟：同一 PID、`NRestarts=0`、丢帧 0、采集 14.9 FPS、`rss_memory_mb` 52.9、`VmRSS` 54,120 kB、39.1 °C，journal 无 error／failed 记录 |
| 盒端清理 | 经只读核查后按用户授权删除构建目录、NCNN 源码、静态 FFmpeg 开发文件、Meson／工具链、配置备份、`/tmp/edge-logging` 安装暂存及旧测试临时文件；`/opt` 仅保留 `edge-vision-analyzer`、`fluent-bit` 与系统 `containerd`，清理后重启并复核加载器 0 缺失、health 200 |
| PC 日志接收恢复 | 初次重启时 PC `:8090` 未监听，Fluent Bit 虽 active 但投递超时并按有限重试丢批，不能称为日志链路已恢复。随后恢复 PC 接收服务，盒端访问 PC `/health` 返回 200；再重启 edge 后查询 PC 日志取得 2 条新记录，均属于 `edge-vision.service`、PID 与该次启动一致，含 1 条 `listening on` 启动日志。只证明后续新日志转发成功，不补传之前丢弃的批次 |
| 正式发布（2026-11-25） | Tag `v1.0-vox` 指向合并提交 `12576f3`；[run 45542161328](https://github.com/nhh37740-glitch/edge-vision-analyzer/actions/runs/45542161328)完成 native 构建、CTest 9/9、pytest 5 项、ARM64 交叉构建与打包，并发布 `edge-vision-analyzer-v1.0-vox-arm64.tar.gz`（65,643,875 字节）。正式附件未在盒端再次安装；盒端验收对象是同一源码树的预发布包。health 版本字符串仍报告 `v0.1-oculus`，见 #160 |
| 后续验收 | 整机重启、长时间运行、真实断网与 LLM／存储事件链路尚未在本次新包工况复验；回退材料仅存联想 PC |

## 3. 各里程碑性能记录

每行对应一套明确的运行工况。原始 API 比例换算为百分比后再填“跳过率”；NCNN 均值与 P95 使用同一取样窗口，llama 端到端和存储入库耗时须在证据中说明起止点。RSS、CPU、温度注明进程／设备与取样口径，连续运行丢帧数同时记录运行时长与计数起点。温度来源包括 `soc_temp_c`；字段不可读时仍不能填造数值。

| 里程碑 | 采集 FPS | 检测 FPS | 跳过率 % | NCNN 均值 ms | NCNN P95 ms | llama-server 端到端 ms | 云存储入库 ms | 魔百盒 RSS MB | CPU % | SoC 温度 °C | 连续运行丢帧数 | 工况／时长／提交／日期／证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| v0.1-oculus | 14.9 | 不适用 | 不适用 | 不适用 | 不适用 | 待实测 | 待实测 | 28.4 | 13.4 | 40.6 | 0 | 本地 MP4 循环，640×480@15；LLM／存储关闭；608 秒窗口；提交 `e7e343a`；2026-11-24 10:57–11:06（北京时间）；`/api/v1/health`、`/api/v1/stats`、`systemctl show` |
| v0.2-discern | 14.9 | 0.1 | ≈99.3（API 显示 1.0） | 40.7 | 待实测 | 待实测 | 待实测 | 28.4 | 13.4 | 40.6 | 0 | 同上；MAD 8.0；另用 PATCH 将阈值临时改为 0.0 强制逐帧检测，快照 OSD 显示 `person 0.71` |
| v0.3-nous | 14.9 | 14.1 | ≈0（强制逐帧；API 累计 0.1） | 39.8 | 待实测 | 待实测（LLM 关闭） | 待实测 | 28.8 | 124.5 | 47.1 | 0 | 本地 MP4 循环，存储启用并指向 PC `10.10.10.1:8090`；PATCH 将 MAD 临时改为 0.0；120 秒窗口（11:17–11:19，北京时间）；1 条事件 `evt_0_1` 与 18,648 字节 JPEG 落库；人物持续在场，事件未结束、冷却未触发 |
| v1.0-vox | 14.9 | 0.1 | ≈99.3（API 显示 1.0） | 40.7 | 待实测 | 待实测 | 待实测 | 28.4 | 13.4 | 40.6 | 0 | 与 v0.2 同一空载窗口；另验收快照 200/`image/jpeg`/640×480、MJPEG 3 秒约 1.0 MB、SIGTERM 约 1 秒退出；盒端 RTSP 推流输出见下一行；RTSP 模拟源取流见再下一行 |
| RTSP 模拟源 | 14.1 | 0.1 | ≈99.3（API 显示 1.0） | 41.4 | 待实测 | 不适用 | 不适用 | 25.1 | 11.3 | 39.6 | 0 | 联想 PC 运行 MediaMTX v1.21.4（RTSP :8554／API :9997），FFmpeg 循环推含人物的本地 `assets/demo_clip_30s.mp4` 至 `/cam`（`testsrc2` 仅用于复现取流，不含人物）；盒端经 PC TCP 中继 `18554→8554` 拉流（直连 8554 被 Windows 防火墙阻止）；640×480@15；MAD 8.0；LLM／存储关闭；60 秒窗口（12:25:40–12:26:40，北京时间）；提交 `e7b1783`；`/api/v1/stats`、`systemctl show`；快照 12,459 字节、MJPEG 3 秒采样 574,449 字节／46 帧 |
| go2rtc ONVIF／RTSP 模拟源 | 14.7 | 0.0 | ≈100（API 显示 1.0） | 42.1 | 待实测 | 不适用 | 不适用 | 24.8 | 12.8 | 38.8 | 0 | 联想 PC 运行 go2rtc v1.9.15（RTSP :8554、API／ONVIF :1984，均绑定回环），exec + libx264 循环 CAVIAR 30 秒素材；盒端经 PC TCP 中继 `18554→8554` 拉流；640×480@15；MAD 8.0；LLM／存储关闭；60 秒窗口（14:55:08–14:56:08，北京时间）；提交 `4eac7a5`；`/api/v1/stats`、`systemctl show`；快照 36,375 字节／640×480、MJPEG 3 秒 1,659,454 字节／46 帧；ONVIF SOAP 200（Profile 固定 1080p30，实际 480p15），PTZ 400，UDP 3702 无响应 |
| CAVIAR 真实视频 | 14.2 | 14.2 | ≈0（强制逐帧；API 显示 0.0） | 40.1 | 待实测 | 不适用 | 不适用 | 25.4 | 126.1 | 45.6 | 0 | CAVIAR `OneStopEnter1cor`（384×288@25；数据集标注 MPEG-2，`ffprobe` 识别 mpeg1video；CC BY-SA）转 640×480@15 H.264，经 MediaMTX + PC TCP 中继拉流；PATCH MAD 0.0；LLM／存储关闭；60 秒窗口（13:22:30–13:23:30，北京时间）；提交 `dcd7534`；`/api/v1/stats`、`systemctl show`；快照 37,677 字节、OSD `person 0.64`、MJPEG 3 秒采样 1,730,064 字节／46 帧；默认 MAD 8.0 时跳过率≈100% |
| 盒端 RTSP 推流输出 | 14.6 | 0.0 | ≈100（API 显示 1.0） | 42.0 | 待实测 | 不适用 | 不适用 | 27.9 | 58.9 | 43.9 | 0 | 同一 CAVIAR 源经 MediaMTX `cam` 拉流；盒端静态 ffmpeg 7.0.2 将 OSD 帧以 `-pix_fmt yuv420p -f rtsp -rtsp_transport tcp` 推回 `/edge`；MAD 8.0；LLM／存储关闭；70 秒窗口（14:18:59–14:20:09，北京时间）；提交 `55fa5eb`；PC 20 秒收到 302 个视频包（≈15.0 FPS），`h264`／Constrained Baseline／`yuv420p`；推流器日志 `frame=2424 time=00:02:41.60 speed=1x`；快照 35,958 字节、MJPEG 3 秒 1,663,846 字节／46 帧；PATCH MAD 0.0 后快照与拉流画面 OSD `person 0.91`／`0.82`／`0.72` |
| 手机流 | 13.8 | 2.7 | ≈80（API 显示 0.8） | 38.0 | 待实测 | 不适用 | 不适用 | 29.1 | 98.1 | 43.3 | 0 | 手机 IP Webcam 1920×1080 MJPEG，经 PC TCP 中继；640×480@15；LLM／存储关闭；60 秒窗口（11:51–11:53，北京时间）；`/api/v1/stats`、`systemctl show` |
| LLM 联动 | 14.9 | 14.1 | ≈0（强制逐帧；API 累计 0.1） | 40.0 | 待实测 | 7744 | 未单独计时 | 27.9 | 124.5 | 46.9 | 0 | 本地 MP4 循环，LLM 与存储启用并指向 PC；PATCH MAD 0.0；60 秒窗口（12:10–12:11，北京时间）；事件 `evt_0_1` 中文摘要与 18,648 字节 JPEG 落库；人物持续在场，事件未结束 |

`CPU %` 为单核百分比：608 秒窗口内 `CPUUsageNSec` 增量 73.394 秒，折算 13.4%；v0.3 行 120 秒窗口增量 149.448 秒，折算 124.5%，即约 1.25 核；手机流行 60 秒增量 58.886 秒，折算 98.1%；LLM 联动行 60 秒增量 74.724 秒，折算 124.5%；RTSP 模拟源行 60 秒增量 6.792 秒，折算 11.3%；CAVIAR 真实视频行 60 秒增量 75.674 秒，折算 126.1%；盒端 RTSP 推流输出行 70 秒增量 41.260739 秒，折算 58.9%；go2rtc 模拟源行 60 秒增量 7.704418 秒，折算 12.8%。`v0.2`/`v1.0` 的跳过率由 14.9 FPS 采集与 0.1 FPS 检测反推，手机流行由 13.8 FPS 采集与 2.7 FPS 检测反推，RTSP 模拟源行由 14.1 FPS 采集与 0.1 FPS 检测反推；go2rtc 模拟源行由 14.7 FPS 采集与 0.0 FPS 检测反推；CAVIAR 行强制逐帧；API 的 `motion_skip_ratio` 四舍五入显示 0.8 或 1.0。NCNN P95 未由 API 暴露，保持待实测；温度为空载稳态 40.6 °C，强制逐帧检测时 44–47 °C。`v0.1` 行只取采集与进程指标，检测／OSD 列归 v0.2／v1.0 验收；同一进程虽运行完整流水线，不把未在该里程碑核验的项写成已通过。v0.3 与 LLM 联动行只验证一条持续事件的投递、摘要与图片读取，未覆盖事件结束、冷却与第二条事件；存储入库耗时未单独计时。

## 4. 优化前后与收益

以下前三项仅为计划对比示例；最后一行是本轮实测的视觉 token 上限对比。其余未实测项继续保持待实测，不能把设计目标作为“优化后”数据。

| 计划对比项 | 对比指标 | 优化前 | 优化后 | 收益 | 同工况证据 |
| --- | --- | --- | --- | --- | --- |
| 开启 MAD 帧差过滤 | 检测调用／跳过率／CPU | 待实测 | 待实测 | 待实测 | 待实测 |
| 开启 FP16／NEON | 单帧推理均值／P95 | 待实测 | 待实测 | 待实测 | 待实测 |
| 无客户端时跳过 MJPEG 编码 | 魔百盒 CPU 占用 | 待实测 | 待实测 | 待实测 | 待实测 |
| 限制视觉 token 上限 | llama-server 端到端／图像 token | 59.4 秒／425 token | 8.45 秒／88 token | 约 86% 延迟降幅 | 同一 640×480 快照，`--image-max-tokens 64`；2026-11-24 |
