# 实测基线与优化收益

本文件记录已核验的魔百盒、联想 PC、手机源、本地 RTSP 模拟源与 CAVIAR 公开真实视频结果；盒端 RTSP 推流输出与 Fluent Bit 仍未采样，保持 **待实测**。记录触发条件以 [AGENTS.md](../AGENTS.md) 为准，项目入口见 [README](../README.md)，指标字段与单位见 [API 契约](api.md)。设计目标、示例配置、硬件称呼和接口示例都不能代替实测；所有尚未实测或核验的值统一写为 **待实测**。

实际记录时同时保留提交、设备与配置、源素材／源协议、负载开关、预热／取样窗口、样本量、连续运行时长和北京时间记录日期，链接对应原始命令输出、PC 日志或采样记录。比较前后保持相同工况，注明采样工具及统计口径；证据先脱敏，不上传私有影像、凭据或本地私有配置。本轮记录 2026-11-24 魔百盒本地视频、手机 IP Webcam 流、盒到 PC 云存储与 LLM 联动，联想侧 MediaMTX + FFmpeg 本地 RTSP 模拟源，以及 CAVIAR 公开真实视频复验；手机流在 11:50–12:07 可用，随后手机端服务停止；盒端 RTSP 推流输出与日志采集仍未采样。

## 1. 真实硬件与运行环境

型号与指令集以设备实际输出为准；`lscpu`、device-tree 等为后续采集来源，不据产品名推断。运行库版本以实际加载版本及证据登记，不能只抄构建配置。

| 核验字段 | 魔百盒（边缘运行） | 联想 PC（构建／推理／存储） | 手机源（视频源） |
| --- | --- | --- | --- |
| 精确型号／SoC（lscpu／/proc/device-tree/model 实读） | `SKYWORTH E900V22C`（device-tree）；lscpu 显示 `Cortex-A53`，未单列 SoC 型号 | `Intel(R) Pentium(R) Gold 6500Y`（Win32_Processor） | 待实测 |
| CPU 架构／核心规格 | aarch64，4 核 Cortex-A53，每核 1 线程（lscpu） | x86_64，2 核 4 线程（Win32_Processor） | 待实测 |
| NEON 指令支持及证据 | 支持；`/proc/cpuinfo` 的 Features 含 `asimd`，不含 `asimddp` | 不适用（x86_64） | 待实测 |
| AVX 指令支持及证据 | 不适用（aarch64） | 待实测（未读取 CPU flags） | 不适用 |
| 内存容量／类型／规格 | 总内存 1935 MiB（`free -m`）；类型／规格未读取 | 17,096,011,776 字节（约 15.9 GiB，Win32_ComputerSystem）；类型／规格未读取 | 待实测 |
| OS 发行版／版本 | Armbian OS 25.02.0 noble（`/etc/os-release`） | Windows 11 Home 10.0.26200 | 待实测 |
| 内核版本 | 6.6.74-happy-new-year aarch64 | 10.0.26200（Windows NT） | 待实测 |
| glibc 运行版本 | 2.39-0ubuntu8.3（`ldd --version`） | 不适用（Windows） | 不适用 |
| FFmpeg 运行版本／动态库主版本 | FFmpeg 7.1.1 静态链接；libavformat 61.7.100、libavcodec 61.19.101、libavutil 59.39.100、libswscale 8.3.100 | RTSP 模拟器使用 FFmpeg N-129241（`g05c674c913`）CLI；`llama-server` 不依赖 FFmpeg | 不适用 |
| NCNN 运行版本 | 源码 tag `20261020`，静态编入 | 不适用 | 不适用 |
| llama.cpp／llama-server 运行版本／提交 | 未在边缘运行；验收时临时开启后关闭 | `b12669`（commit `30ec35ae2`），Windows x64 CPU 构建 | 不适用 |
| 采集软件／视频源协议 | edge-vision-analyzer 读取本地 MP4（H.264）并循环；手机流经 PC 中继时读取 HTTP MJPEG；RTSP 模拟源经 PC TCP 中继读取 H.264；CAVIAR 真实视频转 H.264 后用同一 RTSP 路径 | 运行 `llama-server :8080` 与 FastAPI 云存储 `:8090`；MediaMTX v1.21.4（RTSP :8554／API :9997）+ FFmpeg 推流 | IP Webcam `/shot.jpg` 200（1920×1080）、`/video` 200；硬件与系统版本待实测 |
| 采集证据／日期／输出记录 | 2026-11-24 11:50–11:53、12:25–12:26、13:22–13:23（北京时间）；`/api/v1/health`、`/api/v1/stats`、`journalctl`、`systemctl show` | 2026-11-24 12:10–12:11、12:24–12:26（北京时间）；`/v1/models`、`/health`、存储 `/api/v1/events` 与图片响应、MediaMTX `/v3/paths/list` | 2026-11-24 11:50–12:07（北京时间）；`/shot.jpg` 与 `/video` 响应 |

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
| 内存日志 | journald 实际存储模式／内存文件空间上限 | 待实测（未安装 drop-in） | — |
| 日志采集流控 | Fluent Bit 输入缓冲阈值／存储模式 | 待实测（未安装） | — |

## 3. 各里程碑性能记录

每行对应一套明确的运行工况。原始 API 比例换算为百分比后再填“跳过率”；NCNN 均值与 P95 使用同一取样窗口，llama 端到端和存储入库耗时须在证据中说明起止点。RSS、CPU、温度注明进程／设备与取样口径，连续运行丢帧数同时记录运行时长与计数起点。温度来源包括 `soc_temp_c`；字段不可读时仍不能填造数值。

| 里程碑 | 采集 FPS | 检测 FPS | 跳过率 % | NCNN 均值 ms | NCNN P95 ms | llama-server 端到端 ms | 云存储入库 ms | 魔百盒 RSS MB | CPU % | SoC 温度 °C | 连续运行丢帧数 | 工况／时长／提交／日期／证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| v0.1-oculus | 14.9 | 不适用 | 不适用 | 不适用 | 不适用 | 待实测 | 待实测 | 28.4 | 13.4 | 40.6 | 0 | 本地 MP4 循环，640×480@15；LLM／存储关闭；608 秒窗口；提交 `e7e343a`；2026-11-24 10:57–11:06（北京时间）；`/api/v1/health`、`/api/v1/stats`、`systemctl show` |
| v0.2-discern | 14.9 | 0.1 | ≈99.3（API 显示 1.0） | 40.7 | 待实测 | 待实测 | 待实测 | 28.4 | 13.4 | 40.6 | 0 | 同上；MAD 8.0；另用 PATCH 将阈值临时改为 0.0 强制逐帧检测，快照 OSD 显示 `person 0.71` |
| v0.3-nous | 14.9 | 14.1 | ≈0（强制逐帧；API 累计 0.1） | 39.8 | 待实测 | 待实测（LLM 关闭） | 待实测 | 28.8 | 124.5 | 47.1 | 0 | 本地 MP4 循环，存储启用并指向 PC `10.10.10.1:8090`；PATCH 将 MAD 临时改为 0.0；120 秒窗口（11:17–11:19，北京时间）；1 条事件 `evt_0_1` 与 18,648 字节 JPEG 落库；人物持续在场，事件未结束、冷却未触发 |
| v1.0-vox | 14.9 | 0.1 | ≈99.3（API 显示 1.0） | 40.7 | 待实测 | 待实测 | 待实测 | 28.4 | 13.4 | 40.6 | 0 | 与 v0.2 同一空载窗口；另验收快照 200/`image/jpeg`/640×480、MJPEG 3 秒约 1.0 MB、SIGTERM 约 1 秒退出；盒端 RTSP 推流输出待实测；RTSP 模拟源取流见下一行 |
| RTSP 模拟源 | 14.1 | 0.1 | ≈99.3（API 显示 1.0） | 41.4 | 待实测 | 不适用 | 不适用 | 25.1 | 11.3 | 39.6 | 0 | 联想 PC 运行 MediaMTX v1.21.4（RTSP :8554／API :9997），FFmpeg 循环推含人物的本地 `assets/demo_clip_30s.mp4` 至 `/cam`（`testsrc2` 仅用于复现取流，不含人物）；盒端经 PC TCP 中继 `18554→8554` 拉流（直连 8554 被 Windows 防火墙阻止）；640×480@15；MAD 8.0；LLM／存储关闭；60 秒窗口（12:25:40–12:26:40，北京时间）；提交 `e7b1783`；`/api/v1/stats`、`systemctl show`；快照 12,459 字节、MJPEG 3 秒采样 574,449 字节／46 帧 |
| CAVIAR 真实视频 | 14.2 | 14.2 | ≈0（强制逐帧；API 显示 0.0） | 40.1 | 待实测 | 不适用 | 不适用 | 25.4 | 126.1 | 45.6 | 0 | CAVIAR `OneStopEnter1cor`（384×288@25、MPEG-2、CC BY-SA）转 640×480@15 H.264，经 MediaMTX + PC TCP 中继拉流；PATCH MAD 0.0；LLM／存储关闭；60 秒窗口（13:22:30–13:23:30，北京时间）；提交 `dcd7534`；`/api/v1/stats`、`systemctl show`；快照 37,677 字节、OSD `person 0.64`、MJPEG 3 秒采样 1,730,064 字节／46 帧；默认 MAD 8.0 时跳过率≈100% |
| 手机流 | 13.8 | 2.7 | ≈80（API 显示 0.8） | 38.0 | 待实测 | 不适用 | 不适用 | 29.1 | 98.1 | 43.3 | 0 | 手机 IP Webcam 1920×1080 MJPEG，经 PC TCP 中继；640×480@15；LLM／存储关闭；60 秒窗口（11:51–11:53，北京时间）；`/api/v1/stats`、`systemctl show` |
| LLM 联动 | 14.9 | 14.1 | ≈0（强制逐帧；API 累计 0.1） | 40.0 | 待实测 | 7744 | 未单独计时 | 27.9 | 124.5 | 46.9 | 0 | 本地 MP4 循环，LLM 与存储启用并指向 PC；PATCH MAD 0.0；60 秒窗口（12:10–12:11，北京时间）；事件 `evt_0_1` 中文摘要与 18,648 字节 JPEG 落库；人物持续在场，事件未结束 |

`CPU %` 为单核百分比：608 秒窗口内 `CPUUsageNSec` 增量 73.394 秒，折算 13.4%；v0.3 行 120 秒窗口增量 149.448 秒，折算 124.5%，即约 1.25 核；手机流行 60 秒增量 58.886 秒，折算 98.1%；LLM 联动行 60 秒增量 74.724 秒，折算 124.5%；RTSP 模拟源行 60 秒增量 6.792 秒，折算 11.3%；CAVIAR 真实视频行 60 秒增量 75.674 秒，折算 126.1%。`v0.2`/`v1.0` 的跳过率由 14.9 FPS 采集与 0.1 FPS 检测反推，手机流行由 13.8 FPS 采集与 2.7 FPS 检测反推，RTSP 模拟源行由 14.1 FPS 采集与 0.1 FPS 检测反推；CAVIAR 行强制逐帧；API 的 `motion_skip_ratio` 四舍五入显示 0.8 或 1.0。NCNN P95 未由 API 暴露，保持待实测；温度为空载稳态 40.6 °C，强制逐帧检测时 44–47 °C。`v0.1` 行只取采集与进程指标，检测／OSD 列归 v0.2／v1.0 验收；同一进程虽运行完整流水线，不把未在该里程碑核验的项写成已通过。v0.3 与 LLM 联动行只验证一条持续事件的投递、摘要与图片读取，未覆盖事件结束、冷却与第二条事件；存储入库耗时未单独计时。

## 4. 优化前后与收益

以下前三项仅为计划对比示例；最后一行是本轮实测的视觉 token 上限对比。其余未实测项继续保持待实测，不能把设计目标作为“优化后”数据。

| 计划对比项 | 对比指标 | 优化前 | 优化后 | 收益 | 同工况证据 |
| --- | --- | --- | --- | --- | --- |
| 开启 MAD 帧差过滤 | 检测调用／跳过率／CPU | 待实测 | 待实测 | 待实测 | 待实测 |
| 开启 FP16／NEON | 单帧推理均值／P95 | 待实测 | 待实测 | 待实测 | 待实测 |
| 无客户端时跳过 MJPEG 编码 | 魔百盒 CPU 占用 | 待实测 | 待实测 | 待实测 | 待实测 |
| 限制视觉 token 上限 | llama-server 端到端／图像 token | 59.4 秒／425 token | 8.45 秒／88 token | 约 86% 延迟降幅 | 同一 640×480 快照，`--image-max-tokens 64`；2026-11-24 |
