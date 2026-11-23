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

## 常驻提示（PATCH-009）

### 魔百盒热管理

- 已知限制：无风扇塑料壳内持续运行 NCNN 可能热降频，本项目不按商用 7×24 小时设备承诺持续负载能力。
- 观测（未实测）：后续通过 `/api/v1/stats` 的 `soc_temp_c` 观察温度；当前未连接目标机测温，未验证热降频是否发生。
- 处理：过热先停机降温；如需开盖，只能断电后操作，不能以重启循环代替降温。

### glibc／FFmpeg 兼容性

- 已知风险（未实测）：构建环境与目标机的 glibc／FFmpeg 不匹配可能导致启动或链接失败，当前未核验目标机版本。
- 核对：后续部署规划将打印 `/etc/os-release`、`ldd --version` 和 FFmpeg 主版本，先与交叉构建环境逐项比对。
- 处理：优先在与目标机同发行版、使用目标兼容 sysroot 的 Docker 容器中交叉编译；只有目标确认为 Debian Bookworm 时才选 Bookworm。按 [AGENTS.md 第 4 节](../AGENTS.md) 记录 Discussion；本条尚无已验证解决命令。
