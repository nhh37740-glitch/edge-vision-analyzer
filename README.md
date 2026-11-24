# Edge Vision Analyzer

面向 2GB Armbian 魔百盒的无状态视频分析方案。魔百盒处理手机有线流、运动过滤、目标检测和实时推流，RSS 目标小于 150 MB。联想 PC 提供多模态推理，并独立保存历史事件和未加 OSD 的 JPEG 抓拍。

**当前状态（北京时间 2026-11-24）**：C++17 构建骨架、aarch64 交叉编译工具链、v0.1 核心框架、FFmpeg 采集模块与边缘 REST 控制面已落地（Issue #1–#5），v0.2 的帧差漏斗 MotionNode（64×48 灰度 MAD）、NcnnDetectorNode（NCNN 目标检测与 NMS）与检测量化选型（fp32/fp16/int8）模块已实现，v0.3 的 JPEG／Base64 编码、GenAiWorker 异步投递与 EventTrackerNode 事件聚合也已实现，并统一接入 `src/main.cpp` 的端到端流水线与优雅退出。核心节点、有界队列、配置指标、RGB24 解码、EOF 循环、断线重连、截帧、remux 录片，以及 health、stats、config（含 PATCH 原子热更新）、source-switch、snapshot、stream 端点已实现；控制面已在本机回环验证，离线 CTest 覆盖核心、采集、控制面、帧差漏斗、检测后处理与选型、推流与 transport。推流输出已按 `config.streamer.mode` 装配：`mjpeg` 不启动外部进程，`rtsp` 时 StreamOutNode 对每帧生成独立 OSD 并以 RGB24 推入 RtspPusher 的 FFmpeg 管道；启动失败只告警，MJPEG 端点与流水线继续运行。事件聚合、GenAiWorker、PC 云存储（FastAPI + SQLite）、health 上游探活与 PATCH config 原子热更新已实现；部署脚本与模板已就绪。2026-11-24 已在魔百盒完成离线原生构建、systemd 部署与本地视频端到端验收（health、带 OSD 快照、MJPEG、PATCH、SIGTERM 优雅退出），盒到 PC 云存储的事件与图片投递也已验收；手机 IP Webcam 流（经 PC 中继）与 llama-server LLM 联动已验收；联想侧 MediaMTX + FFmpeg 本地 RTSP 模拟源及盒端取流已验收；go2rtc ONVIF／RTSP 模拟源评估完成（仅模拟器 ONVIF 子集，边缘程序未实现 ONVIF 客户端）；盒端 RTSP 推流输出也已验收（H.264/yuv420p、15 FPS）；Fluent Bit 与日志采集仍待验收。

## 接力入口

先阅读 [AGENTS.md](AGENTS.md)，再读以下四份依据；Claude Code 入口见 [CLAUDE.md](CLAUDE.md)。

| 文件 | 正式内容 |
| --- | --- |
| [docs/architecture.md](docs/architecture.md) | 硬件拓扑、流水线、内存和线程约束、开源参考 |
| [docs/api.md](docs/api.md) / [OpenAPI](docs/openapi.yaml) | 边缘实时 API、联想 PC 云存储 API 与上游推理契约 |
| [docs/roadmap.md](docs/roadmap.md) | 两阶段数据源、完整交付件、节点内部设计与离线验收 |
| [config/config.example.json](config/config.example.json) | 局域网示例，运行前复制为 `config/config.local.json` |

可写入简历的技术栈按证据分档见 [docs/resume-stack.md](docs/resume-stack.md)。

## 硬件与网络

手机通过 Type-C 转网口扩展坞、魔百盒通过网线、联想奔腾 PC 通过网线，三台分别接入同一以太网交换机。手机与魔百盒不使用 USB 直连。有 PD 口的手机扩展坞可单独供电。交换机接上级路由器时使用 DHCP；独立离线组网时手动设置同网段静态 IP，详见架构文档。

示例静态地址：联想 `192.168.10.10`（`llama-server :8080`、[云存储模拟服务](cloud-storage/README.md) `:8090`）、魔百盒 `192.168.10.11`（API `:8081`）、手机 `192.168.10.12`（示例 IP Webcam `:8080/video`），掩码 `255.255.255.0`。均为示例，接力时按现场修改。运行流量只在交换机局域网内，不接入公网云 API。

## 里程碑进度

| Milestone / 后续发版 Tag | 意象 | 交付目标 |
| --- | --- | --- |
| `v0.1-oculus` | Oculus，神目 / 视觉 | 视频源接入、截帧录片、基础控制面 |
| `v0.2-discern` | Discern，洞察 / 明辨 | 帧差漏斗与 NCNN 端侧检测 |
| `v0.3-nous` | Nous，神思 / 心智 | 事件聚合、异步推理与 PC 端图文留存、查询 |
| `v1.0-vox` | Vox，神谕之声 / 宣示 | OSD、MJPEG / RTSP 与 systemd 守护 |

阶段 A 在用户安排的真机验收中采集手机流并 remux 录制 15–30 秒 `assets/demo_clip.mp4`；阶段 B 改为本地视频 `loop: true`，后续开发与压测脱离手机无人值守运行。录片及模型权重不入库；YOLO-Fastest-v2／NanoDet-Plus 的获取与放置见 [models/README.md](models/README.md)，测试不自动下载权重。

## 性能摘要

| 版本 | 采集 FPS | 检测 FPS | NCNN 单帧 P95（ms） | 魔百盒 RSS（MB） | SoC 温度（°C） |
| --- | --- | --- | --- | --- | --- |
| `v0.1-oculus` | 14.9 | 不适用 | 待实测 | 28.4 | 40.6 |
| `v0.2-discern` | 14.9 | 0.1 | 待实测 | 28.4 | 40.6 |
| `v0.3-nous` | 14.9 | 14.1 | 待实测 | 28.8 | 47.1 |
| `v1.0-vox` | 14.9 | 0.1 | 待实测 | 28.4 | 40.6 |
| 手机流 | 13.8 | 2.7 | 待实测 | 29.1 | 43.3 |
| LLM 联动 | 14.9 | 14.1 | 待实测 | 27.9 | 46.9 |
| RTSP 模拟源 | 14.1 | 0.1 | 待实测 | 25.1 | 39.6 |
| go2rtc 模拟源 | 14.7 | 0.0 | 待实测 | 24.8 | 38.8 |
| CAVIAR 真实视频 | 14.2 | 14.2 | 待实测 | 25.4 | 45.6 |
| 盒端 RTSP 推流输出 | 14.6 | 0.0 | 待实测 | 27.9 | 43.9 |

详细硬件、实际运行参数、压测工况与优化收益见 [实测台账](docs/benchmarks.md)。每个里程碑发版前按实测结果同步摘要。

## 联想宿主机接力

```sh
git clone https://github.com/nhh37740-glitch/edge-vision-analyzer.git
cd edge-vision-analyzer
git switch feature/capture
```

在阅读冷启动文件并收到后续开发指令后，按 Issue 接续开发；v0.1–v0.3 与 v1.0 的主要模块已接入运行入口并在魔百盒完成本地视频与 PC 存储联动验收，手机流、LLM 联动、本地 RTSP 模拟源取流与盒端 RTSP 推流输出已验收，go2rtc ONVIF／RTSP 模拟源评估完成；Fluent Bit 与日志采集仍待验收。预建任务见 [GitHub Issues](https://github.com/nhh37740-glitch/edge-vision-analyzer/issues)，阶段归档见 [Milestones](https://github.com/nhh37740-glitch/edge-vision-analyzer/milestones)。

构建设计：联想 x86_64 Linux 支持本机离线测试构建，并交叉编译 aarch64 运行产物；魔百盒只运行，不编译大型依赖。根 [CMakeLists.txt](CMakeLists.txt) 已启用 C++17，当前可构建 `edge_core`、`edge_common`、`edge_capture`、`edge_detector`、`edge_streamer`、`edge_transport`、`edge_api`、`edge-vision-analyzer`、`test_build`、`test_core`、`test_capture`、`test_detector`、`test_streamer`、`test_transport` 与 `test_api`，其余静态库随里程碑加入。`cmake/aarch64-toolchain.cmake` 使用 `aarch64-linux-gnu-g++`。

```sh
# 本机构建与离线单测；首次配置会下载并编译 ncnn（固定 Release Tag 20261020）
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-native --parallel 2
ctest --test-dir build-native --output-on-failure --no-tests=error

# aarch64 交叉编译（不运行目标机测试）
cmake -S . -B build-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake -DBUILD_TESTING=OFF
cmake --build build-arm64 --parallel 2
```

本机构建基线（ubuntu-24.04／MSYS2 UCRT64）：CMake 3.31.6、Ninja 1.13.2、GCC 13.3.0、GoogleTest 1.14.0；FFmpeg 库版本为 libavformat 60.16.100、libavcodec 60.31.102、libavutil 58.29.100、libswscale 7.5.100；NCNN 固定 Release Tag `20261020`（BSD-3-Clause），由 CMake FetchContent 首次配置时下载并编译。常规 PR 的 `ci-gate` 不运行 C++ 构建，C++ 变更在提交前本地完成构建与 CTest；aarch64 交叉编译和完整测试只在 `v*` Tag 的 Release 工作流执行。测试不下载模型、不等待真实设备或推理。

## PC 双服务启动

`scripts/start_pc_services.sh` 在联想 PC 后台启动两个独立进程：`llama-server`（默认 `:8080`）与 `cloud-storage`（默认 `:8090`）。模型与多模态投影路径必须由环境变量提供，脚本不内置本机路径：

```sh
export LLM_MODEL=/path/to/model.gguf
export LLM_MMPROJ=/path/to/mmproj.gguf
# 可选：LLM_PORT、STORAGE_PORT、LLM_ALIAS（默认 vlm-local）、LOG_DIR、PROBE_TRIES
bash scripts/start_pc_services.sh
```

脚本把日志写入 `mktemp -d` 生成的临时目录，就绪时打印两个 PID 与日志路径；只有 `http://127.0.0.1:8080/health` 与 `http://127.0.0.1:8090/health` 都通过才报告就绪。缺少 `llama-server`、`uvicorn` 或 `curl` 时提示安装方式并返回非零。启动或探活失败时打印对应日志尾部，并提示用 `kill <PID>` 停止已启动的进程。脚本不下载模型、不读取 `config/config.local.json`，日志位于临时目录且不提交。

## 本地相机模拟器

手机 IP Webcam 会随锁屏停止监听，不能作为无人值守验收源。联想 PC 可用 [MediaMTX](https://github.com/bluenviron/mediamtx) v1.21.4 与 FFmpeg 提供确定性的 RTSP 源；MediaMTX 不含 ONVIF 发现或 Profile S，本次只验收 RTSP 取流。实测命令如下，素材与二进制不入库：

```yaml
# mediamtx.local.yml
api: yes
apiAddress: :9997
rtspAddress: :8554
paths:
  cam:
```

```sh
# 1. 启动 MediaMTX（默认 RTSP :8554、API :9997；最小配置启用 API 与 cam path）
mediamtx mediamtx.local.yml
# 2. 新环境可先生成 30 秒测试图；它不含人物，只验证取流、快照与 MJPEG
ffmpeg -f lavfi -i testsrc2=size=640x480:rate=15 -t 30 -c:v libx264 -pix_fmt yuv420p assets/demo_clip_30s.mp4
# 3. 循环推送；重编码避免 B 帧与循环点造成的解码错误、首帧延迟
ffmpeg -re -stream_loop -1 -i assets/demo_clip_30s.mp4 -an -r 15 \
  -c:v libx264 -preset ultrafast -tune zerolatency -g 15 -pix_fmt yuv420p -b:v 800k \
  -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam
# 4. 盒端 input.source 指向 rtsp://<联想 LAN IP>:8554/cam 后重启服务；以下 curl 在盒端执行
curl -s http://127.0.0.1:8081/api/v1/stats
```

Windows 首次监听会弹防火墙确认；若盒端 TCP 8554 超时而 PC 本机回环可达，说明入站仍被拦截，需删除或禁用 `mediamtx.exe` 的阻止规则并放行 TCP 8554（9997 仅供 PC 本机查询 MediaMTX API），或临时经 PC TCP 中继转发。2026-11-24 实测中 MediaMTX 命中已有入站阻止规则；盒端 `input.source` 临时改为 `rtsp://10.10.10.1:18554/cam`，PC 上运行 `10.10.10.1:18554 → 127.0.0.1:8554` 的临时 TCP 转发后完成 60 秒验收。回环可达不能代替局域网验收。`GET /api/v1/stats` 实测 `capture_fps` 14.1、`dropped_frames` 0，快照 640×480、MJPEG 流头为 `multipart/x-mixed-replace`。与真实 IP 相机相比，该模拟源没有 ONVIF 发现、相机鉴权和相机端编码差异。

### go2rtc（RTSP 与 ONVIF 子集）

需要 ONVIF 控制面时，可用 [go2rtc](https://github.com/AlexxIT/go2rtc) v1.9.15（MIT，Windows 单文件 `go2rtc.exe`）；下载包二进制与素材不入库。它原生提供 RTSP 服务与 ONVIF Device／Media 子集，适合验证客户端接入流程；没有 WS-Discovery、PTZ 和相机鉴权，Profile 返回值也是固定桩，不能代表真实相机能力。ONVIF 是控制面，媒体仍由 RTSP 承载，两者必须分开验收。

| 候选 | 许可证／平台 | 能力 | 结论 |
| --- | --- | --- | --- |
| go2rtc 1.9.15 | MIT／Windows 单文件 | RTSP + ONVIF Device／Media 子集 | 采用为 ONVIF 控制面与 RTSP 模拟源 |
| MediaMTX 1.21.4 + FFmpeg | MediaMTX 为 MIT；FFmpeg 依构建为 LGPL-2.1+ 或 GPL（本机含 libx264，属 GPL）／Windows | RTSP／RTMP／HLS 等，无 ONVIF | 保留为纯 RTSP 基线与推流接收端 |
| [onvif-vcam](https://github.com/ithouse98/onvif-vcam) | 公开源码但未标注许可证／Python | ONVIF Profile S／T、WS-Discovery、RTSP | 未实测：能力更完整，但依赖重且授权不明 |
| ptzsim | MIT／Python 3.8+ | ONVIF（含 PTZ） | 未实测：需 Python 依赖，启动成本高于单文件 go2rtc |
| Happytime ONVIF Server | 许可不明确（旧版 freeware，现版疑商业授权）／Windows | ONVIF Profile S／G／T | 未采用：闭源且授权不清 |

前两项已完成真机实测；后三项只做资料核对，因依赖、许可证或可复现性未采用。

以下配置把三个监听都限制在回环，避免 Windows 防火墙弹窗；启动后 Web UI、API 与 ONVIF 共用 `127.0.0.1:1984`。把 `<FFMPEG_EXE>` 与 `<VIDEO_FILE>` 换成本机路径。

```yaml
# go2rtc.local.yaml
api:
  listen: "127.0.0.1:1984"
rtsp:
  listen: "127.0.0.1:8554"
webrtc:
  listen: "127.0.0.1:8555"
streams:
  cam: 'exec:<FFMPEG_EXE> -re -stream_loop -1 -i <VIDEO_FILE> -r 15 -c:v libx264 -preset ultrafast -tune zerolatency -g 15 -pix_fmt yuv420p -f rtsp {output}'
```

```sh
# 1. 启动；浏览器打开 http://127.0.0.1:1984/ 可看流状态
go2rtc.exe -c go2rtc.local.yaml
# 2. 媒体面：确认编码、分辨率与帧率
ffprobe -rtsp_transport tcp -select_streams v:0 \
  -show_entries stream=codec_name,width,height,pix_fmt,avg_frame_rate \
  -of default=nw=1 rtsp://127.0.0.1:8554/cam
# 3. ONVIF 控制面：设备信息；GetProfiles、GetStreamUri 同法发往设备或媒体服务
curl -s -H 'Content-Type: application/soap+xml' \
  --data '<soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope" xmlns:tds="http://www.onvif.org/ver10/device/wsdl"><soap:Body><tds:GetDeviceInformation/></soap:Body></soap:Envelope>' \
  http://127.0.0.1:1984/onvif/device_service
```

`ffmpeg:` 源加 `#input=file` 只按实时速度播放一遍，30 秒素材到点即结束；需要无人值守循环时改用上面的 `exec:` 并保留 `-stream_loop -1`。2026-11-24 用 CAVIAR 30 秒素材实测：PC 本机连续拉流 40 秒收到 601 帧；盒端经 `10.10.10.1:18554 → 127.0.0.1:8554` 中继完成 60 秒验收（14:55:08–14:56:08），采集 14.7 FPS、检测 0.0 FPS、丢帧 0、RSS 24.8 MB、CPU 12.8%（单核百分比）、SoC 38.8 °C，快照 36,375 字节／640×480，MJPEG 3 秒 1,659,454 字节／46 帧。ONVIF 侧以原生 SOAP 核验 `GetSystemDateAndTime`、`GetCapabilities`、`GetProfiles`、`GetStreamUri`、`GetSnapshotUri` 与 `GetDeviceInformation` 均返回 200，`GetSnapshotUri` 指向的 640×480 JPEG 可读取；`GetProfiles` 固定报告 1920×1080@30 Main，而实际流为 640×480@15，PTZ 请求返回 400，UDP 3702 无响应。盒端仍只以 RTSP 取流，边缘程序不含 ONVIF 客户端；不能把本次结果写成程序已支持 ONVIF。

### 公开真实视频源

需要真实人物画面时，可用 Edinburgh CAVIAR 数据集的走廊视角 `OneStopEnter1cor`（原始 384×288、25fps；数据集页标注 MPEG-2，`ffprobe` 识别为 mpeg1video）。数据公开可用，许可证为 CC BY-SA；发布结果须致谢 EC Funded CAVIAR project/IST 2001 37540，下载页见 <http://homepages.inf.ed.ac.uk/rbf/CAVIAR/>。视频只留本机临时目录，不入库：

```sh
curl -L -o OneStopEnter1cor.mpg \
  https://homepages.inf.ed.ac.uk/rbf/CAVIARDATA2/OneStopEnter1cor/OneStopEnter1cor.mpg
ffmpeg -fflags +genpts -stream_loop 3 -i OneStopEnter1cor.mpg -an \
  -vf "scale=640:480,fps=15" -t 30 -c:v libx264 -preset ultrafast \
  -tune zerolatency -g 15 -pix_fmt yuv420p -b:v 1200k caviar_30s.mp4
```

把上述 `caviar_30s.mp4` 代入模拟器推流命令即可。2026-11-24 用该源并经 PATCH 将 MAD 临时改为 0.0 后，60 秒实测采集／检测均 14.2 FPS、丢帧 0，快照 640×480 且 OSD 标出 `person 0.64`，MJPEG 3 秒 1.73 MB；默认 MAD 8.0 时这段慢速走廊画面几乎全被跳过，需按场景调阈值或强制检测。CAVIAR 素材是低分辨率老录像，不代表现代 IP 相机的画质与编码特性。

### 盒端 RTSP 推流输出

盒端没有 ffmpeg CLI，现有静态 FFmpeg 也未编译 RTSP muxer 与 libx264；推流前在联想下载 aarch64 静态 FFmpeg 7.0.2（含 libx264 与 RTSP muxer）并安装到魔百盒 `/usr/local/bin/ffmpeg`，二进制与素材不入库。MediaMTX 配置同时启用 `cam` 与 `edge`；盒端 `streamer.mode` 改为 `rtsp`、`rtsp_url` 指向 PC 中继的 `/edge`，采集源仍为 `/cam`。核对命令：

```sh
# 1. 盒端确认编码器与 muxer
ffmpeg -hide_banner -encoders 2>/dev/null | grep libx264
ffmpeg -hide_banner -muxers 2>/dev/null | grep ' rtsp'
# 2. PC 拉流核对像素格式与帧率
ffprobe -rtsp_transport tcp -select_streams v:0 \
  -show_entries stream=codec_name,profile,width,height,pix_fmt,avg_frame_rate \
  -of default=nw=1 rtsp://127.0.0.1:8554/edge
```

2026-11-24 实测：PC 20 秒收到 302 个视频包（约 15.0 FPS），`h264`／Constrained Baseline／640×480／`yuv420p`；盒端推流器日志 `frame=2424 time=00:02:41.60 speed=1x`。MAD 8.0 的 70 秒窗口采集 14.6 FPS、检测 0.0 FPS、丢帧 0、RSS 27.9 MB、CPU 58.9%（单核百分比）、SoC 43.9 °C；快照 35,958 字节、MJPEG 3 秒 1,663,846 字节／46 帧。PATCH 将 MAD 临时改为 0.0 后，快照与拉流画面均带 OSD（`person 0.91`、`person 0.82`／`0.72`）。经 PC 中继时需注意：`socket.create_connection(timeout=10)` 会把读超时一并设为 10 秒，推流约 10 秒被 TEARDOWN；中继需在连接成功后 `settimeout(None)`。该链路仍只有 RTSP，不含 ONVIF。

## 魔百盒部署（脚本与模板已就绪；2026-11-24 完成首次真机验收）

`scripts/deploy_to_box.sh` 按“对时 → 打印系统与库版本 → 发包 → 重启 → 探活 → 输出最近 20 行日志”执行；任一步失败返回非零并打印 `journalctl -u "$SERVICE" -n 20 --no-pager`。本次部署按相同步骤手工执行；脚本直接覆盖运行中可执行文件的问题已由 Issue #117 修复，修复后在魔百盒复跑通过。手机流、LLM 联动、本地 RTSP 模拟源取流与盒端 RTSP 推流输出已验收；Fluent Bit 与日志采集仍未验收。

盒到 PC 云存储联动另行验证：盒端 `storage.endpoint` 指向联想 PC `10.10.10.1:8090`，health 显示 `storage: reachable`；PATCH 将 MAD 临时改为 0.0 后，PC 收到事件 `evt_0_1`（person 0.817）与 18,648 字节 JPEG，查询与图片读取均返回 200。LLM 摘要仍关闭，事件持续在场未结束，冷却与第二条事件未覆盖。

首次真机验收使用 `SKYWORTH E900V22C`（Armbian 25.02.0 noble，Cortex-A53×4，1935 MiB 内存）与本地 `assets/demo_clip_30s.mp4` 循环：`/api/v1/health` 返回 200 且 `CaptureNode` 为 `running`，采集 14.9 FPS，NCNN 平均 40.7 ms，进程 RSS 28.4 MB（systemd `MemoryMax=300M`），10 分钟丢帧 0，SoC 40.6 °C。快照返回 640×480 JPEG 并带 OSD，MJPEG 流头为 `multipart/x-mixed-replace; boundary=frame`，PATCH 阈值热更新与非法值 400 已核验，SIGTERM 约 1 秒退出。构建与版本细节、未验证项见[实测台账](docs/benchmarks.md)；由于目标机无外网且缺少 aarch64 sysroot，本次二进制在盒上离线原生构建，后续发版仍按 Release 工作流交叉编译。

前置条件：

- 联想 PC 已交叉编译 `build-arm64/edge-vision-analyzer`，并准备公开配置模板 `config/config.example.json`；不要传递本机 `config/config.local.json`、模型权重或令牌。
- 目标机可 SSH 登录，具备 `sudo`、`curl` 与 systemd；已创建 `/opt/edge-vision-analyzer/bin`、`/opt/edge-vision-analyzer/config`，部署用户可写。
- 按需安装 [edge-vision.service](deploy/edge-vision.service)、[journald-edge.conf](deploy/journald-edge.conf) 与 [fluent-bit.conf](deploy/fluent-bit.conf)；参数契约见[架构文档](docs/architecture.md)第 9、10 节。

示例地址按现场替换：

```sh
export EDGE_BOX_HOST=192.168.10.11
bash scripts/deploy_to_box.sh "$EDGE_BOX_HOST" \
  build-arm64/edge-vision-analyzer \
  config/config.example.json \
  edge-vision
```

脚本把构件放入目标机 `/opt/edge-vision-analyzer/bin/`。目标机已有 `config/config.local.json` 时默认保留现场配置，只提示覆盖方式；首次部署（文件不存在）或显式设置 `EDGE_CONFIG_FORCE=1` 时才把配置模板写到该路径。随后重启 `edge-vision.service` 并在目标机循环请求 `http://127.0.0.1:8081/api/v1/health`。目标机目录需预先准备，脚本不保存也不传递 sudo 密码。

公开模板的 `input.dump_interval_sec` 默认为 0，即不周期性写盘；调试确需截帧时再显式设置。640×480、每 2 秒一张的估算写入量约 860MB/天，会耗尽盒上可用空间。

失败排查：

- 任一步失败时脚本会输出最近 20 行服务日志；先核对单元路径、配置文件与 sudo 权限。
- 对时失败：确认 SSH 用户可执行 `sudo date`，按提示输入目标机密码。
- scp 失败：确认目标目录存在且可写，构件确为 ARM64 产物。
- 探活失败：检查 `config.local.json` 的端口、`ffmpeg` 与 glibc 版本，以及 `systemctl status edge-vision`。

## 仓库协作

仓库公开，只上传本项目契约与骨架。初始提交之后所有修改经 PR，仓库仅允许 Squash and merge，并自动删除合并后的分支。main 保护及状态检查接入约定见 [AGENTS.md](AGENTS.md)；`ci-gate` 是主线唯一必需检查。PR 还会运行 DeepSeek 自动评审，作为辅助信息，不替代 `ci-gate`；评审密钥保存在 GitHub Actions Secrets，不写入仓库。边缘和存储分支检查按改动启用，其中 C++ 构建由本地验证承担；发版流程需具备全部构建、测试和打包输入。发行包提供配置模板，部署时创建自己的 `config/config.local.json`。联想宿主机从与 `main` 同步的 `feature/capture` 接力。
