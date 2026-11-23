# Edge Vision Analyzer

面向 2GB Armbian 魔百盒的有界内存视频分析方案：手机有线视频流 → 帧差过滤 → NCNN 目标检测 → 事件聚合 → 联想 PC `llama-server` 多模态描述 → OSD 与视频输出。

**当前状态（北京时间 2026-11-23）**：仅初始化目录、配置与设计契约。全部业务能力、构建系统、测试、模型及部署均待实现；当前没有可运行程序、性能实测或发布 Tag。本轮遵守 PATCH-001 + PATCH-002，禁止开始业务开发、编译或创建 PR。

## 接力入口

先阅读 [AGENTS.md](AGENTS.md)，再读以下四份依据；Claude Code 入口见 [CLAUDE.md](CLAUDE.md)。

| 文件 | 正式内容 |
| --- | --- |
| [docs/architecture.md](docs/architecture.md) | 硬件拓扑、流水线、内存和线程约束、开源参考 |
| [docs/api.md](docs/api.md) / [OpenAPI](docs/openapi.yaml) | 边缘 API 与上游推理接口契约 |
| [docs/roadmap.md](docs/roadmap.md) | 两阶段数据源、完整交付件、节点内部设计与离线验收 |
| [config/config.json](config/config.json) | 无私密数据的统一示例配置 |

## 硬件与网络

手机通过 Type-C 转网口扩展坞、魔百盒通过网线、联想奔腾 PC 通过网线，三台分别接入同一以太网交换机。手机与魔百盒不使用 USB 直连。有 PD 口的手机扩展坞可单独供电。交换机接上级路由器时使用 DHCP；独立离线组网时手动设置同网段静态 IP，详见架构文档。

示例静态地址：联想 `192.168.10.10`（`llama-server :8080`）、魔百盒 `192.168.10.11`（API `:8081`）、手机 `192.168.10.12`（示例 IP Webcam `:8080/video`），掩码 `255.255.255.0`。均为示例，接力时按现场修改。

## 里程碑（均待实现）

| Milestone / 后续发版 Tag | 意象 | 交付目标 |
| --- | --- | --- |
| `v0.1-oculus` | Oculus，神目 / 视觉 | 视频源接入、截帧录片、基础控制面 |
| `v0.2-discern` | Discern，洞察 / 明辨 | 帧差漏斗与 NCNN 端侧检测 |
| `v0.3-nous` | Nous，神思 / 心智 | 事件聚合与异步多模态分析 |
| `v1.0-vox` | Vox，神谕之声 / 宣示 | OSD、MJPEG / RTSP 与 systemd 守护 |

阶段 A 在用户安排的真机验收中采集手机流并 remux 录制 15–30 秒 `assets/demo_clip.mp4`；阶段 B 改为本地视频 `loop: true`，后续开发与压测脱离手机无人值守运行。录片及模型权重不入库。

## 联想宿主机接力

```sh
git clone https://github.com/nhh37740-glitch/edge-vision-analyzer.git
cd edge-vision-analyzer
git switch feature/capture
```

在阅读冷启动文件并收到后续开发指令后，再实现 `v0.1-oculus`。预建任务见 [GitHub Issues](https://github.com/nhh37740-glitch/edge-vision-analyzer/issues)，阶段归档见 [Milestones](https://github.com/nhh37740-glitch/edge-vision-analyzer/milestones)。

构建设计：联想 x86_64 Linux 支持本机离线测试构建，并交叉编译 aarch64 运行产物；魔百盒只运行，不编译大型依赖。`CMakeLists.txt`、`cmake/aarch64-toolchain.cmake` 和运行入口均属于后续交付，本轮仅 `cmake/.gitkeep`，因此尚无可执行的编译或运行命令。正式构建命令随工具链 Issue 落地后补齐，测试不下载模型、不等待真实设备或推理。

## 仓库协作

仓库公开，只上传本项目契约与骨架。初始提交之后所有修改经 PR，仓库仅允许 Squash and merge，并自动删除合并后的分支。main 保护及状态检查接入约定见 [AGENTS.md](AGENTS.md)；本轮只启用状态检查开关，具体交叉编译/单测 CI 后续接入，不能视为已验证。初始化完成后停在 `feature/capture`，不创建 PR。
