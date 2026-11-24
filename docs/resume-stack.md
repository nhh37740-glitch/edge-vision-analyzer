# 简历技术栈与证据清单

本文件回应 Issue #134：把仓库里真实使用过的嵌入式与边缘计算名词按证据分档，避免把规划项写成已掌握。所有数值来自 [实测台账](benchmarks.md)；命令中的 `<盒地址>` 按现场替换，不在公开记录写私有机密。

## 1. 分档定义

- **A｜已实现并真机验证**：代码已合入，且在魔百盒或联想 PC 上有可复核的实测记录。
- **B｜已实现但未真机验证／未验收**：代码、配置或工作流已存在，但缺目标机运行或发版证据。
- **C｜规划或仅模拟器验证**：只有设计、模板或第三方模拟器结论，不能写成产品能力。

## 2. 技术栈与证据

| 技术项 | 档位 | 证据 | 简历表述建议 |
| --- | --- | --- | --- |
| Armbian 25.02／aarch64／Cortex-A53×4／1935 MiB | A | [benchmarks](benchmarks.md) 硬件表；本地视频 608 秒验收 | 在 2 GB 级 ARM64 盒上完成端到端部署与资源约束验收 |
| C++17／CMake／Ninja／静态库分层 | A | `CMakeLists.txt`、`cmake/aarch64-toolchain.cmake`、提交 `d046c5b` | 用 C++17 与 CMake 拆分核心、采集、检测、推流、API 模块 |
| FFmpeg libavformat／libavcodec／sws_scale／remux | A | `src/capture/`、提交 `886060a`、采集单测 | 基于 FFmpeg C API 实现 RTSP／MJPEG 拉流、RGB24 解码与仅视频 remux |
| NCNN YOLO-Fastest-v2＋NMS，2 线程，fp32 | A | `src/detector/`、提交 `0374498`；检测均值约 38–42 ms | 在 ARM64 上部署 NCNN 目标检测与 NMS；P95 尚未采集，不写“低延迟调优” |
| fp16／int8 量化选型与回退 | B | 检测量化代码与单测；盒端无 `asimddp`，实际回退 fp32 | 实现量化候选与运行时回退；真机未完成量化对照 |
| NEON | B | NCNN aarch64 后端默认启用；无 A/B 实测 | 只说“运行于 NEON 可用的 ARM64”，不写“NEON 优化” |
| 有界队列与满时丢最旧 | A | `src/core/bounded_queue.hpp`、提交 `3f9c45d`／`f4a7a5c` | 用有界队列隔离采集、检测与推流，10 分钟丢帧 0 |
| systemd 托管＋cgroup `MemoryMax=300M`＋journal | A | `deploy/edge-vision.service`、[benchmarks](benchmarks.md) 参数表 | 用 systemd 托管服务并设置 cgroup 内存上限；SIGTERM 约 1 秒退出 |
| journald volatile 模板 | C | `deploy/journald-edge.conf` 模板未安装 | 不写“已配置 journald 限额” |
| aarch64 交叉编译与 Release | B | `cmake/aarch64-toolchain.cmake`、`.github/workflows/release.yml`；尚无 Release，盒端为离线原生构建 | 写“已配置交叉编译工作流”，不写“已发版” |
| 静态 FFmpeg CLI 推流 | A | `/usr/local/bin/ffmpeg` 7.0.2-static；提交 `4eac7a5` | 用静态 FFmpeg 将 OSD 帧编码为 H.264／yuv420p 并推 RTSP |
| RTSP 取流／推流＋MediaMTX | A | 提交 `dcd7534`、`4eac7a5`；约 15 FPS | 搭建 MediaMTX＋FFmpeg 可重复源，完成盒端 RTSP 输入与输出验收 |
| ONVIF | C | 提交 `fa2d8cb`；仅 go2rtc 模拟器 Device／Media 子集，`src/` 无 ONVIF 客户端 | 写“ONVIF 模拟器控制面联调”，不写“支持 ONVIF” |
| TCP 中继 | A（临时验收手段） | README 模拟器章节；未进入仓库功能 | 不写“实现中继服务”；只写“为绕过防火墙临时转发” |
| RSS／温度／FPS／推理时延指标 | A | `src/common/metrics.cpp`、[benchmarks](benchmarks.md) | 采集 RSS、SoC 温度、采集／检测 FPS；CPU 由 systemd `CPUUsageNSec` 外部测算 |
| cpp-httplib REST／MJPEG／PATCH | A | `src/api/`、提交 `00ef395`／`e7e343a` | 用 cpp-httplib 提供 health、stats、config 热更新、快照与 MJPEG |
| GoogleTest／CTest | A | `tests/`，8 个 C++ 测试目标、65 个 `TEST` | 用 GoogleTest／CTest 建立离线回归，覆盖采集、检测、API 与推流 |
| FastAPI＋SQLite 云存储 | A | `cloud-storage/server.py`、提交 `5ff610a`／`a16cdb5` | 用 FastAPI＋SQLite 接收事件、保存 JPEG 并提供历史查询 |
| llama.cpp＋Qwen2-VL-2B Q4_K_M | A | 提交 `e7b1783`、[benchmarks](benchmarks.md) | 接入 llama.cpp 多模态推理；单次描述 7.744 s，视觉 token 调优 59.4→8.45 s |
| GitHub Actions | A（PR 策略／Python）／B（C++ 发版） | `.github/workflows/ci.yml` 已运行；`release.yml` 未发版 | 写“PR 策略与 Python 检查”，C++／ARM 发版标为待验收 |
| 离线构建 | A | [踩坑记录](troubleshooting.md) 盒上原生构建条目 | 在无外网目标机上离线构建并部署静态依赖 |

## 3. 简历 bullet 草案

- 在 2 GB 级 Armbian／Cortex-A53 盒上以 C++17 构建有界队列多线程流水线：FFmpeg libav* 解码 → 帧差 → NCNN 检测 → OSD → MJPEG／RTSP；空载 10 分钟丢帧 0、RSS 28.4 MB、SoC 40.6 °C。
- 用 systemd 托管并设置 cgroup `MemoryMax=300M`、`Restart=always` 与 journal 日志；SIGTERM 约 1 秒优雅退出。
- 搭建 MediaMTX＋FFmpeg 可重复 RTSP 源与接收端，完成盒端 H.264／yuv420p／约 15 FPS 推流；另以 go2rtc 完成 ONVIF 模拟器控制面子集联调。
- 边缘事件异步投递至 FastAPI＋SQLite，接入 llama.cpp Qwen2-VL-2B；视觉 token 调优把单次描述从 59.4 s 降到 8.45 s。

> ONVIF、交叉编译发版与 Fluent Bit 日志采集不能写成“已掌握”；分别按 C、B、B 档表述。

## 4. 扩展候选

| 候选 | 收益 | 成本／风险 | 验收方式 |
| --- | --- | --- | --- |
| systemd watchdog＋`sd_notify` | 发现拉流线程假死并自动重启 | 小／中：心跳必须绑定最近成功取帧，否则上游断流会误重启 | `Type=notify`、`WatchdogSec`；`kill -STOP` 模拟卡死后服务在阈值内重启并恢复取流 |
| 零依赖 Prometheus `/metrics` | 复用现有指标做可观测性 | 小／低：新增文本 exposition，不引入客户端库 | 输出 Prometheus 0.0.4 文本；PC 抓取后 Grafana 建采集／检测 FPS、RSS／温度、丢帧／LLM 时延 3 个面板 |
| MQTT 告警（Mosquitto） | 事件推送到局域网订阅端 | 中／中：QoS1 与 LWT 需处理重连；边缘无磁盘队列，必须定义丢弃策略 | 断网重连不阻塞流水线；broker 恢复后收到告警；内存与丢帧保持有界 |
| Fluent Bit 日志链路 | 补齐 journald→PC 结构化日志 | 小／中：目标机无外网，需离线安装并验证权限 | 日志在 PC `/api/v1/logs` 可查询，断连重试不丢服务 |
| systemd 沙箱加固 | 降低服务权限面 | 小／低：需逐项验证设备与文件访问 | `systemd-analyze security` 评分改善，采集、快照、推流仍正常 |

## 5. 排除项

- **GStreamer**：与现有 FFmpeg 流水线功能重叠，引入第二套媒体栈会增加镜像与排障成本。
- **PREEMPT_RT**：15 FPS 视觉流水线没有硬实时截止期，实时内核收益不足。
- **只读根文件系统**：项目已用内存日志与无状态落盘替代；在厂商启动链上改造风险高。
- **Docker／eBPF**：当前 2 GB 目标机与无外网部署不划算，暂不引入。

## 6. 复验命令

```sh
# 资源限制、cgroup 版本与运行指标；<盒地址> 按现场替换
ssh root@<盒地址> 'systemctl show edge-vision -p MemoryMax; \
  stat -fc %T /sys/fs/cgroup; curl -s http://127.0.0.1:8081/api/v1/stats'
# 若已启用 RTSP 推流，核对编码与帧率
ffprobe -rtsp_transport tcp -select_streams v:0 \
  -show_entries stream=codec_name,width,height,pix_fmt,avg_frame_rate \
  -of default=nw=1 rtsp://<盒地址>:8554/edge
```

## 7. 维护规则

- 新增技术项必须先有代码、文档或实测证据，再更新本表；未验证项保持 B／C 档。
- 每次真机验收后同步 [benchmarks](benchmarks.md)，本表只引用、不复制未记录的新数值。
- 简历表述不使用“精通”“大规模”“高并发”等无法由本项目证据支撑的词。
