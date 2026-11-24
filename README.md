# Edge Vision Analyzer

面向 2GB Armbian 魔百盒的无状态视频分析方案。魔百盒处理手机有线流、运动过滤、目标检测和实时推流，RSS 目标小于 150 MB。联想 PC 提供多模态推理，并独立保存历史事件和未加 OSD 的 JPEG 抓拍。

**当前状态（北京时间 2026-11-24）**：C++17 构建骨架、aarch64 交叉编译工具链、v0.1 核心框架、FFmpeg 采集模块与边缘 REST 控制面已落地（Issue #1–#5），v0.2 的帧差漏斗 MotionNode（64×48 灰度 MAD）、NcnnDetectorNode（NCNN 目标检测与 NMS）与检测量化选型（fp32/fp16/int8）模块已实现，v0.3 的 JPEG／Base64 编码、GenAiWorker 异步投递与 EventTrackerNode 事件聚合也已实现，并统一接入 `src/main.cpp` 的端到端流水线与优雅退出。核心节点、有界队列、配置指标、RGB24 解码、EOF 循环、断线重连、截帧、remux 录片，以及 health、stats、config（含 PATCH 原子热更新）、source-switch、snapshot、stream 端点已实现；控制面已在本机回环验证，离线 CTest 覆盖核心、采集、控制面、帧差漏斗、检测后处理与选型、推流与 transport。推流输出已按 `config.streamer.mode` 装配：`mjpeg` 不启动外部进程，`rtsp` 时 StreamOutNode 对每帧生成独立 OSD 并以 RGB24 推入 RtspPusher 的 FFmpeg 管道；启动失败只告警，MJPEG 端点与流水线继续运行。事件聚合、GenAiWorker、PC 云存储（FastAPI + SQLite）、health 上游探活与 PATCH config 原子热更新已实现；部署脚本与模板已就绪。2026-11-24 已在魔百盒完成离线原生构建、systemd 部署与本地视频端到端验收（health、带 OSD 快照、MJPEG、PATCH、SIGTERM 优雅退出），盒到 PC 云存储的事件与图片投递也已验收；手机 IP Webcam 流（经 PC 中继）与 llama-server LLM 联动已验收；联想侧 MediaMTX + FFmpeg 本地 RTSP 模拟源及盒端取流已验收；盒端 RTSP 推流输出、Fluent Bit 与日志采集仍待验收。

## 接力入口

先阅读 [AGENTS.md](AGENTS.md)，再读以下四份依据；Claude Code 入口见 [CLAUDE.md](CLAUDE.md)。

| 文件 | 正式内容 |
| --- | --- |
| [docs/architecture.md](docs/architecture.md) | 硬件拓扑、流水线、内存和线程约束、开源参考 |
| [docs/api.md](docs/api.md) / [OpenAPI](docs/openapi.yaml) | 边缘实时 API、联想 PC 云存储 API 与上游推理契约 |
| [docs/roadmap.md](docs/roadmap.md) | 两阶段数据源、完整交付件、节点内部设计与离线验收 |
| [config/config.example.json](config/config.example.json) | 局域网示例，运行前复制为 `config/config.local.json` |

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

详细硬件、实际运行参数、压测工况与优化收益见 [实测台账](docs/benchmarks.md)。每个里程碑发版前按实测结果同步摘要。

## 联想宿主机接力

```sh
git clone https://github.com/nhh37740-glitch/edge-vision-analyzer.git
cd edge-vision-analyzer
git switch feature/capture
```

在阅读冷启动文件并收到后续开发指令后，按 Issue 接续开发；v0.1–v0.3 与 v1.0 的主要模块已接入运行入口并在魔百盒完成本地视频与 PC 存储联动验收，手机流、LLM 联动与本地 RTSP 模拟源取流已验收；盒端 RTSP 推流输出、Fluent Bit 与日志采集仍待验收。预建任务见 [GitHub Issues](https://github.com/nhh37740-glitch/edge-vision-analyzer/issues)，阶段归档见 [Milestones](https://github.com/nhh37740-glitch/edge-vision-analyzer/milestones)。

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

## 本地 RTSP 相机模拟器

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
# 2. 循环推送公开测试素材，避免 B 帧与超高码率造成的首帧延迟
ffmpeg -re -stream_loop -1 -i assets/demo_clip_30s.mp4 -an -r 15 \
  -c:v libx264 -preset ultrafast -tune zerolatency -g 15 -pix_fmt yuv420p -b:v 800k \
  -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam
# 3. 盒端 input.source 指向 rtsp://<联想 LAN IP>:8554/cam 后重启服务
curl -s http://127.0.0.1:8081/api/v1/stats
```

Windows 首次监听会弹防火墙确认；若盒端 TCP 8554 超时而 PC 本机回环可达，说明入站仍被拦截，需放行 `mediamtx.exe` 的 TCP 8554／9997，或临时经 PC TCP 中继转发。2026-11-24 实测中 MediaMTX 命中已有入站阻止规则，盒端经本机中继 `18554→8554` 完成 60 秒验收；回环可达不能代替局域网验收。`GET /api/v1/stats` 实测 `capture_fps` 14.1、`dropped_frames` 0，快照 640×480、MJPEG 流头为 `multipart/x-mixed-replace`。与真实 IP 相机相比，该模拟源没有 ONVIF 发现、相机鉴权和相机端编码差异。

## 魔百盒部署（脚本与模板已就绪；2026-11-24 完成首次真机验收）

`scripts/deploy_to_box.sh` 按“对时 → 打印系统与库版本 → 发包 → 重启 → 探活 → 输出最近 20 行日志”执行；任一步失败返回非零并打印 `journalctl -u "$SERVICE" -n 20 --no-pager`。本次部署按相同步骤手工执行；脚本直接覆盖运行中可执行文件的问题已由 Issue #117 修复，修复后在魔百盒复跑通过。手机流、LLM 联动与本地 RTSP 模拟源取流已验收；盒端 RTSP 推流输出、Fluent Bit 与日志采集仍未验收。

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
