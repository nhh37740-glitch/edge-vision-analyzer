# Edge Vision Analyzer

面向 2GB Armbian 魔百盒的无状态视频分析方案。魔百盒处理手机有线流、运动过滤、目标检测和实时推流，RSS 目标小于 150 MB。联想 PC 提供多模态推理，并独立保存历史事件和未加 OSD 的 JPEG 抓拍。

**当前状态（北京时间 2026-11-24）**：C++17 构建骨架、aarch64 交叉编译工具链、v0.1 核心框架、FFmpeg 采集模块与边缘 REST 控制面已落地（Issue #1–#5），v0.2 的帧差漏斗 MotionNode（64×48 灰度 MAD）、NcnnDetectorNode（NCNN 目标检测与 NMS）与检测量化选型（fp32/fp16/int8）模块已实现，v0.3 的 JPEG／Base64 编码、GenAiWorker 异步投递与 EventTrackerNode 事件聚合也已实现，并统一接入 `src/main.cpp` 的端到端流水线与优雅退出。核心节点、有界队列、配置指标、RGB24 解码、EOF 循环、断线重连、截帧、remux 录片，以及 health、stats、config、source-switch、snapshot、stream 端点已实现；控制面已在本机回环验证，离线 CTest 覆盖核心、采集、控制面、帧差漏斗、检测后处理与选型、推流与 transport。推流输出已按 `config.streamer.mode` 装配：`mjpeg` 不启动外部进程，`rtsp` 时 StreamOutNode 对每帧生成独立 OSD 并以 RGB24 推入 RtspPusher 的 FFmpeg 管道；启动失败只告警，MJPEG 端点与流水线继续运行。事件聚合、GenAiWorker、PC 存储已实现；部署脚本与模板就绪，真机阶段 A、RTSP／MediaMTX 联动与 v1.0 验收待执行。

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
| `v0.1-oculus` | 待实测 | 待实测 | 待实测 | 待实测 | 待实测 |
| `v0.2-discern` | 待实测 | 待实测 | 待实测 | 待实测 | 待实测 |
| `v0.3-nous` | 待实测 | 待实测 | 待实测 | 待实测 | 待实测 |
| `v1.0-vox` | 待实测 | 待实测 | 待实测 | 待实测 | 待实测 |

详细硬件、实际运行参数、压测工况与优化收益见 [实测台账](docs/benchmarks.md)。每个里程碑发版前按实测结果同步摘要。

## 联想宿主机接力

```sh
git clone https://github.com/nhh37740-glitch/edge-vision-analyzer.git
cd edge-vision-analyzer
git switch feature/capture
```

在阅读冷启动文件并收到后续开发指令后，再实现 `v0.1-oculus`。预建任务见 [GitHub Issues](https://github.com/nhh37740-glitch/edge-vision-analyzer/issues)，阶段归档见 [Milestones](https://github.com/nhh37740-glitch/edge-vision-analyzer/milestones)。

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

## 魔百盒部署（脚本与模板已就绪，真机验收待执行）

`scripts/deploy_to_box.sh` 按“对时 → 打印系统与库版本 → 发包 → 重启 → 探活 → 输出最近 20 行日志”执行；任一步失败返回非零并打印 `journalctl -u "$SERVICE" -n 20 --no-pager`。模板与脚本不等于已部署：真机验收待执行，当前不声称魔百盒已运行本服务或日志已由 Fluent Bit 接管。

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

脚本把构件放入目标机 `/opt/edge-vision-analyzer/bin/`，把配置模板落到 `/opt/edge-vision-analyzer/config/config.local.json`，重启 `edge-vision.service` 后在目标机循环请求 `http://127.0.0.1:8081/api/v1/health`。目标机目录需预先准备，脚本不保存也不传递 sudo 密码。

失败排查：

- 任一步失败时脚本会输出最近 20 行服务日志；先核对单元路径、配置文件与 sudo 权限。
- 对时失败：确认 SSH 用户可执行 `sudo date`，按提示输入目标机密码。
- scp 失败：确认目标目录存在且可写，构件确为 ARM64 产物。
- 探活失败：检查 `config.local.json` 的端口、`ffmpeg` 与 glibc 版本，以及 `systemctl status edge-vision`。

## 仓库协作

仓库公开，只上传本项目契约与骨架。初始提交之后所有修改经 PR，仓库仅允许 Squash and merge，并自动删除合并后的分支。main 保护及状态检查接入约定见 [AGENTS.md](AGENTS.md)；`ci-gate` 是主线唯一必需检查。PR 还会运行 DeepSeek 自动评审，作为辅助信息，不替代 `ci-gate`；评审密钥保存在 GitHub Actions Secrets，不写入仓库。边缘和存储分支检查按改动启用，其中 C++ 构建由本地验证承担；发版流程需具备全部构建、测试和打包输入。发行包提供配置模板，部署时创建自己的 `config/config.local.json`。联想宿主机从与 `main` 同步的 `feature/capture` 接力。
