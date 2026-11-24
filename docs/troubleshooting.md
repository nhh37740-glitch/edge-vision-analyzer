# 踩坑记录

编译、安装、工具链和依赖配置等环境问题优先发到本仓库 [Discussions](https://github.com/nhh37740-glitch/edge-vision-analyzer/discussions) 的 `General` 或 `Q&A`。`gh` 低于 2.94.0 或当前环境没有 Discussion 命令时，在本文件追加。已解决的环境问题也在这里留一条不超过 5 行的记录，便于接力查找。逻辑缺陷或当前任务的阻塞问题按 [AGENTS.md 第 4 节](../AGENTS.md) 登记 Issue。

环境问题的 Discussion 以 `[排错]` 开头。正文长度和后续复盘格式遵循 [AGENTS.md 第 4 节](../AGENTS.md)。命令和日志先脱敏。尚未解决时在「解决命令」写「待验证」，解决后补上已验证命令。

## 三段式模板

```markdown
- 现象与命令：<日期／环境，执行的命令和关键现象>
- 根因（一句话）：<原因>
- 解决命令：<已验证的命令和结果，可附 Discussion 链接>
```

## 已登记记录

- 现象与命令：Win11 运行监听 `0.0.0.0` 的 `edge-vision-analyzer.exe` 或 `test_capture.exe` 首次弹 Windows Defender 防火墙确认，需人工允许。
- 根因：新可执行文件没有已登记的入站规则；仅绑定 `127.0.0.1` 的回环监听不会触发。
- 解决命令：点击允许后规则生效；API 测试改用 `127.0.0.1`，正式服务保留 `0.0.0.0`。见 [讨论 #30](https://github.com/nhh37740-glitch/edge-vision-analyzer/discussions/30)。

- 现象与命令：服务运行时 `deploy_to_box.sh` 直接 scp 覆盖 `/opt/edge-vision-analyzer/bin/edge-vision-analyzer`，报 `scp: dest open ... Failure`。
- 根因：Linux 禁止写入正在执行的二进制，返回 `ETXTBSY`；上传为同目录 `.new` 文件成功。
- 解决命令：脚本改为先传 `.new`，再由目标机 `mv -f` 原子替换并 `chmod 0755`；服务重启后加载新文件。

- 现象与命令：2026-11-24 魔百盒无外网，`cmake`／`ninja`／`pkg-config`／`ffmpeg` 均缺失，`apt-get update` 与 GitHub 下载不可用。
- 根因：目标机未预装构建工具，也没有 aarch64 sysroot 可供交叉链接。
- 解决命令：在联想侧下载 arm64 CMake 3.31.6、Ninja 1.13.1 与 meson wheel，scp 后在盒上构建 pkgconf 2.3.0 和最小静态 FFmpeg 7.1.1，再离线原生编译。仅为本次兜底，发版仍走 Release 交叉构建。
- 偏差：本次兜底把 FFmpeg 静态编入二进制，与 PATCH-005 的动态库约定不同；发版交叉构建仍使用动态库。

- 现象与命令：盒上首次编译报 `fatal error: nlohmann/adl_serializer.hpp: No such file`。
- 根因：只复制了单文件 `json.hpp`，该版本按拆分头文件包含 `<nlohmann/...>`。
- 解决命令：复制完整 `nlohmann/` 头目录到 `/opt/headers/nlohmann` 后重新编译通过。

- 现象与命令：解包后 `tar` 报多数文件时间戳在未来，`make` 提示 clock skew。
- 根因：魔百盒系统时间停在 2025-07，比构建机慢约 16 个月。
- 解决命令：部署前用联想当前时间执行 `date -s` 对时；`deploy_to_box.sh` 已含该步骤。

- 现象与命令：手机 `192.168.0.103` 可 ping 通，但 `curl http://192.168.0.103:8080/shot.jpg` 返回连接拒绝；启动手机 IP Webcam 的 Start server 后恢复 200。
- 根因：IP Webcam 服务随手机锁屏或应用退到后台而停止监听，主机在线不代表端口可用。
- 解决命令：验收前保持手机屏幕常亮并确认 `/shot.jpg` 返回 200；服务停止后盒端 `CaptureNode` 会重连失败，需要重新启动手机服务。

- 现象与命令：Windows 运行 llama.cpp b12669 报 `VCRUNTIME140.dll` 或 `api-ms-win-crt-heap-l1-1-0.dll` 缺失。
- 根因：系统缺少可被加载器搜索的 UCRT 转发库与 MSVC 运行库，Git Bash 不会自动使用 `System32\downlevel`。
- 解决命令：把 `C:\Windows\System32\downlevel\api-ms-win-crt-*.dll` 和已安装应用中的 `vcruntime140*.dll`、`msvcp140*.dll`、`concrt140.dll` 复制到 `llama-server.exe` 同目录；更规范的做法是安装 VC++ 运行库。

- 现象与命令：Qwen2-VL-2B 默认处理 640×480 快照需 425 个图像 token，llama-server 端到端约 59.4 秒，超过盒端 25 秒读超时。
- 根因：CPU 视觉编码耗时随图像 token 数增长，默认视觉分辨率过高。
- 解决命令：llama-server 增加 `--image-max-tokens 64`，同一快照降至 88 token、端到端 8.45 秒，盒端可正常返回摘要。

- 现象与命令：2026-11-24 魔百盒 `/opt/edge-vision-analyzer/snapshots/` 累计 330 张 JPEG、6.7MB，部署模板的 `dump_interval_sec` 为 2。
- 根因：部署脚本把公开模板覆盖为 `config.local.json`，采集节点按间隔持续写盘；640×480 每 2 秒一张约 860MB/天，与无状态边缘契约冲突。
- 解决命令：模板与结构默认值改为 0，盒上配置改为 0 并清空 `snapshots/`，重启后 health 200；目标机已有配置默认保留，覆盖需 `EDGE_CONFIG_FORCE=1`。

- 现象与命令：2026-11-24 联想 PC 运行 MediaMTX v1.21.4 并由 FFmpeg 推送 `rtsp://127.0.0.1:8554/cam` 后，PC 本机可拉流，盒端访问 `10.10.10.1:8554` 超时。
- 根因：Windows 防火墙已存在 `mediamtx.exe` 入站阻止规则；回环测试不经过入站检查，不能证明局域网可达。
- 解决命令：在 Windows 防火墙删除或禁用 `mediamtx.exe` 的入站阻止规则，再允许 TCP 8554 入站（9997 仅供 PC 本机查询 API）。本次无管理员权限，临时用 PC 本机 TCP 中继 `18554→8554`；盒端 60 秒采集 14.1 FPS、丢帧 0。

- 现象与命令：Issue #130 允许 ONVIF 或 RTSP 模拟源；MediaMTX v1.21.4 不提供 ONVIF 发现或 Profile S。
- 根因：MediaMTX 是 RTSP／RTMP／HLS 等传输服务，不是 ONVIF 相机实现。
- 解决命令：本轮只验收 RTSP 取流；ONVIF 如确需支持应另开 Issue，不把 RTSP 验收写成 ONVIF 已支持。

- 现象与命令：FFmpeg 以 `-c:v copy` 循环推 MP4 到 MediaMTX 后，盒端解码持续报 `co located POCs unavailable` 与 `mmco: unref short failure`。
- 根因：带 B 帧的 MP4 原包经 RTSP 重封装后时间戳／参考帧不完整，循环点尤易触发。
- 解决命令：改为 `-r 15 -c:v libx264 -preset ultrafast -tune zerolatency -g 15 -pix_fmt yuv420p` 重新编码后再推流，解码报错消失、采集 14.1 FPS。

- 现象与命令：CAVIAR `OneStopEnter1cor.mpg` 用 `ffprobe` 查时长仅 0.11 秒，但 `-count_frames` 能读出 269 帧。
- 根因：MPEG-1 文件缺少可靠时间戳，容器时长字段不可信；直接转码会得到异常时间轴。
- 解决命令：转码时加 `-fflags +genpts`，用 `-vf fps=15` 重设帧率并 `-t 30` 截取；CAVIAR 为 CC BY-SA，使用时致谢项目，视频不入库。

## 常驻提示（PATCH-009）

### 魔百盒热管理

- 已知限制：无风扇塑料壳内持续运行 NCNN 可能热降频，本项目不按商用 7×24 小时设备承诺持续负载能力。
- 观测：2026-11-24 本地视频空载 608 秒窗口内 `soc_temp_c` 为 40.6 °C，临时强制逐帧检测最高 45.9 °C；未做长时间满载压测。
- 处理：过热先停机降温；如需开盖，只能断电后操作，不能以重启循环代替降温。

### glibc／FFmpeg 兼容性

- 已核验：魔百盒 glibc 2.39-0ubuntu8.3，运行静态链接的 FFmpeg 7.1.1（libavformat 61.7.100、libavcodec 61.19.101），服务可正常启动解码。
- 核对：部署脚本打印 `/etc/os-release`、`ldd --version` 和 FFmpeg 主版本，与构建环境逐项比对。
- 处理：发版优先在与目标机同发行版、使用目标兼容 sysroot 的 Docker 容器中交叉编译；按 [AGENTS.md 第 4 节](../AGENTS.md) 记录 Discussion。
