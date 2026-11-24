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

- 现象与命令：2026-11-24 魔百盒 `/opt/edge-vision-analyzer/snapshots/` 累计 330 张 JPEG、6.7MB，部署模板的 `dump_interval_sec` 为 2。
- 根因：部署脚本把公开模板覆盖为 `config.local.json`，采集节点按间隔持续写盘；640×480 每 2 秒一张约 860MB/天，与无状态边缘契约冲突。
- 解决命令：模板与结构默认值改为 0，盒上配置改为 0 并清空 `snapshots/`，重启后 health 200；目标机已有配置默认保留，覆盖需 `EDGE_CONFIG_FORCE=1`。

## 常驻提示（PATCH-009）

### 魔百盒热管理

- 已知限制：无风扇塑料壳内持续运行 NCNN 可能热降频，本项目不按商用 7×24 小时设备承诺持续负载能力。
- 观测：2026-11-24 本地视频空载 608 秒窗口内 `soc_temp_c` 为 40.6 °C，临时强制逐帧检测最高 45.9 °C；未做长时间满载压测。
- 处理：过热先停机降温；如需开盖，只能断电后操作，不能以重启循环代替降温。

### glibc／FFmpeg 兼容性

- 已核验：魔百盒 glibc 2.39-0ubuntu8.3，运行静态链接的 FFmpeg 7.1.1（libavformat 61.7.100、libavcodec 61.19.101），服务可正常启动解码。
- 核对：部署脚本打印 `/etc/os-release`、`ldd --version` 和 FFmpeg 主版本，与构建环境逐项比对。
- 处理：发版优先在与目标机同发行版、使用目标兼容 sysroot 的 Docker 容器中交叉编译；按 [AGENTS.md 第 4 节](../AGENTS.md) 记录 Discussion。
