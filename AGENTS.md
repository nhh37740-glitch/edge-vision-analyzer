# AGENTS.md — edge-vision-analyzer 开发契约

## 冷启动必读
在写代码前，按顺序阅读本文件、`docs/architecture.md`、`docs/api.md`、`docs/roadmap.md`、`config/config.example.json`；接口机器契约见 `docs/openapi.yaml`，排障记录见 [docs/troubleshooting.md](docs/troubleshooting.md)。这些仓库文件是接力依据，不依赖聊天记录。执行范围以用户最新指令为准。

## 0. 初始化与接力边界（PATCH-001 至 PATCH-011）
- PATCH-003 的无状态边缘与 PC 存储分工有效。PATCH-004/005 补充协作模板、GitHub Actions 和构建契约。PATCH-006 规定踩坑分流。PATCH-007 更新局域网运行、资源释放和中文协作约定。PATCH-008 补充边缘日志备份。PATCH-009 登记部署脚本、对时、硬件限制和温度观测。PATCH-010 规定设计帖和工程复盘。PATCH-011 建立硬件、运行参数和实测台账。
- 初始化交付为文档、配置和骨架。C++ 与 Python 服务实现、依赖工具链、实际单测和部署留待后续开发。当前只核验工作流和文件结构，初始化门禁允许空模块通过。发版前必须具备可全量构建和测试的源文件与打包输入。
- 初始化文档和骨架通过绑定 Issue 的 PR 走 Squash 合并。合并后快进同步 `feature/capture`，核对远端 `main` 与该分支后停止。接手联想宿主机的 Agent 在收到后续开发指令后再实施业务功能。

## 1. 硬件拓扑、构建与存储约束
- **宿主机（开发 / 交叉编译 / 推理 / 存储）**：联想盒子电脑（2021 年奔腾 6 系列，x86_64 Linux，16GB DDR3）。负责 `aarch64` 交叉编译，独立运行 `llama-server`（`:8080`）和云存储模拟服务（`:8090`）。
- **边缘目标机（运行端）**：魔百盒（晶晨 Amlogic ARMv8 4 核 Cortex-A53，Armbian，2GB RAM）。严禁在魔百盒上直接编译大型 C++ 依赖。
- **资源硬红线**：魔百盒进程常驻 RSS 必须小于 150 MB。帧节点输入队列默认容量 2，满时丢弃最旧帧。异步事件队列容量 1，只保留最新待处理事件。
- **无状态边缘**：魔百盒仅保留实时处理所需的状态，历史事件与抓拍由联想 PC 持久化并查询。禁止在边缘实现历史事件环形表或失败帧落盘兜底。阶段 A 用户安排的取样截帧和 remux 录片属于显式验收动作，仍按路线图执行。
- **异步投递**：`GenAiWorker` 对干净抓拍只编码一次。开启 LLM 时先获取摘要，失败使用空摘要。开启存储时投递同一 JPEG 与结构化结果，超时或失败后结束本次任务，视频流水线继续处理。
- **云端事件留存**：`cloud-storage/` 单独维护 FastAPI、SQLite 和图片文件，与边缘编译无关。存储文件和抓拍只写联想 PC 的 Git 忽略目录，接口和 DDL 分别见 `docs/api.md` 与 `cloud-storage/schema.sql`。
- **本地运行**：手机视频源、魔百盒、联想 PC 的推理与存储只在交换机局域网内通信，不接入公网云 API。仓库只提交 `config/config.example.json`，部署时复制为被 Git 忽略的 `config/config.local.json`。
- **物理网络**：手机经 Type-C 以太网扩展坞、魔百盒和联想 PC 各自接入同一以太网交换机，不以 USB 直连手机与魔百盒。DHCP / 离线静态地址方案见 `docs/architecture.md`。

### 魔百盒资源释放与日志

- FFmpeg `AVFormatContext*`、`AVCodecContext*`、`AVFrame*`、`AVPacket*` 和 `SwsContext*` 均用带自定义 deleter 的 `std::unique_ptr` 托管。每次处理完 `AVFrame` 和 `AVPacket` 立即 `unref`，打开的格式、解码和转换上下文在切源或退出时按生命周期释放。禁止另启内存清理进程或脚本。
- `GenAiWorker` 向联想 PC `:8090` 提交图片和结构化结果后，无论成功或失败，立即释放帧引用及 JPEG／Base64 缓冲。关闭存储时在当前处理步骤结束后释放。魔百盒不缓存失败结果，不积压重试。
- 魔百盒程序只向 `stdout/stderr` 写日志，由 systemd 接管。禁止程序把日志文件写到魔百盒磁盘。`deploy/journald-edge.conf` 仅使用运行时 Journal，`deploy/fluent-bit.conf` 将该服务的日志独立转发至联想 PC。模板和容量边界见 [架构文档](docs/architecture.md)。
- `deploy/edge-vision.service` 使用 `MemoryMax=300M`、`Restart=always` 兜底。日常常驻 RSS 目标仍为小于 150 MB。单元配置属于部署模板，须在目标机验收后使用。

### 部署与硬件基线（PATCH-009）

- `scripts/deploy_to_box.sh` 和 `scripts/start_pc_services.sh` 当前仅为带注释的占位，后续每个脚本不得超过 50 行。魔百盒部署按“对时、打印系统与库版本、发包、重启、探活、输出最近 20 行服务日志”执行。启动或探活失败必须返回非零状态。联想 PC 启动脚本分别探活 `:8080` 和 `:8090`。
- 部署前用联想 PC 当前时间给魔百盒对时。边缘事件时间异常时，PC 以服务端时间统一入库和计算图片日期目录。阈值和 `soc_temp_c` 温度字段见 [API 契约](docs/api.md)，温度只用于观测压测，不参与温控调度。
- 发热降频、glibc 与 FFmpeg 版本对齐见 [踩坑记录](docs/troubleshooting.md)。部署只打印排障所需版本，不新增反复校验环境的脚本。

## 2. 严格禁止事项（防过度防御与过度验证）
必须遵守“够用即止”的工程原则，严禁出现以下行为：
1. **禁止编写校验型防御脚本**：严禁编写 SHA256/MD5 校验、工具链自检脚本、二进制完整性验证器或反复校验环境变量的 wrapper 脚本。
2. **禁止过度参数防御**：内部模块间调用直接信任已定义的 struct/interface，不要为内部函数堆砌几十行冗余的空指针、范围或格式重复校验。
3. **禁止无关重构**：只修改当前里程碑涉及的文件，不要顺手格式化、重命名或重构无关模块。
4. **禁止阻塞式假验证**：不要在构建或测试流程中自动下载模型权重、自动拉起真实摄像头或阻塞等待真实 LLM 推理返回。
5. **禁止自创线程胶水架构**：所有处理阶段必须继承 `BaseNode` 并通过 `attach_next()` 串联，严禁在 `main.cpp` 中手写散装线程和多重队列。

## 3. 单元测试边界（克制测试）
每个核心模块（`capture` / `detector` / `transport` / `streamer`）必须附带 GoogleTest + CTest 单元测试，但严格控制体量：
- **每个模块仅需 3~5 个测试用例**：覆盖核心正常路径（2~3 个）与最常见异常（1~2 个，如非法路径、空帧输入、HTTP 超时）。
- **纯离线、极速运行**：使用 `tests/fixtures/` 下的极小测试素材或 Mock 对象，整个 `ctest` 套件必须在 5 秒内完成。
- 不为 getter/setter、纯数据结构或日志打印编写测试。

## 4. 中文协作与问题分流（PATCH-006／007／010）

Issue、PR、评审评论、Discussion 帖子与回复用通顺的中文和完整句子。说清谁做了什么，结果如何。不要写成电报体，不堆砌无关的缩写和黑话。命令、路径、必要的原始报错和关联关键词可保留原文。

示例：“修复了视频读到末尾时没有重置解码器缓冲区的问题，本地循环播放测试已通过。”

| 内容 | 上限与必填信息 |
| --- | --- |
| Issue 正文 | ≤ 400 字，不含代码块。有报错时只摘核心日志 10～15 行。 |
| PR 描述 | ≤ 600 字。写明关联 Issue、改动清单和本地编译与单测结果。 |
| Issue、PR、评审和 Discussion 的评论或回复 | ≤ 250 字。直接给结论和命令。 |
| Discussion `[排错]` | ≤ 350 字。固定三段：现象与命令、根因、解决命令。 |
| Discussion `[设计]`／`[复盘]` | 500～800 字。结构和发帖时机见下文。 |

PR 首行的 Issue 关联规则继续生效。`pr-policy` 排除围栏代码块后统计剩余正文字符，超过 700 字符时阻止合并。PR 写作上限仍为 600 字。评论与回复的上限通过本契约执行。

遇到问题按以下规则记录，不得静默绕过：

1. 编译、安装、工具链及依赖配置等环境小坑记入本仓库 GitHub Discussions 的 `General` 或 `Q&A`，标题以 `[排错]` 开头，优先使用 `gh discussion create` 和 `gh discussion comment`。`gh` 低于 2.94.0 或当前环境不支持该命令时，直接追加到 [docs/troubleshooting.md](docs/troubleshooting.md)。已解决的环境或编译问题，无论是否已发 Discussion，都在该文件留一条不超过 5 行的记录。
2. 代码逻辑缺陷或阻塞当前任务的问题开 GitHub Issue，标签使用 `type:bug,agent-log`。按任务模板写明范围及验收标准，包括复现命令、关键报错、一句话根因和当前状态。修复 PR 首行写 `Fixes #<issue_id>`，合并时关闭对应 Issue。

### 设计记录与工程复盘（PATCH-010）

- `[设计]`：在开始编写 `v0.1-oculus`、`v0.2-discern`、`v0.3-nous` 或 `v1.0-vox` 的业务代码前，到本仓库 Discussions 的 `General` 或 `Show and tell` 为该版本发一篇设计帖。说明数据流、内存与性能的取舍、备选方案，以及为什么选择当前方案。每版本一篇，后续方案变化补充原帖。
- `[复盘]`：每解决一个需要技术排查的真实问题，就在同一讨论范围发一篇复盘。单纯的拼写改动不触发。FFmpeg 内存泄漏、多线程竞争、交叉编译 ABI 冲突和推流卡顿都属于此类。写清排查过程和底层机制，可链接对应 Issue 或已有排错记录。
- 两类深度帖都按四段写：背景痛点、方案对比／根因分析、实现或排查步骤、核心知识点小结。正文须为 500～800 字。跟帖评论最多 250 字。
- 面向公开仓库，用完整的中文句子客观说明证据、取舍和结果，讲清为什么。采用工程决策记录或故障复盘的口吻，不使用「面试话术」「八股总结」等表述，不堆砌术语。
- 设计帖和复盘以实际开工或已解决的问题为触发条件。初始化仅登记规则。

## 5. Git 与发版规范
- Commit 和 PR 标题使用 `<type>(<scope>): <中文说明>`，例如 `feat(capture): 实现 FFmpeg 拉流与本地录片`。
- 一个 Issue 只对应一个模块的一项明确功能，或一个 `agent-log` 具体报错。使用 `.github/ISSUE_TEMPLATE/task.md` 写明涉及目录／文件和可验证的验收标准。禁止混入跨里程碑工作。实现按所列范围修改文件。
- 一个 Issue 对应一个 PR。使用 `.github/pull_request_template.md`，正文首行写 `Closes #<issue_id>` 或 `Fixes #<issue_id>`。只关联当前仓库一个打开的 Issue，不能附带关闭其他 Issue 或提交无对应 Issue 的 PR。
- **严禁直接向 `main` 分支提交或推送代码。**
- 分支命名：`feature/<module>`、`fix/<issue-id>`。
- 常规 C++ 开发提交前做两步验证：`cmake --build` 编译通过 + `ctest --output-on-failure` 通过。验证通过即可提 PR，不要重复多轮多余检查。
- `main` 的唯一必需状态检查是 `ci-gate`。所有指向 `main` 的 PR 均运行策略和改动识别，云存储检查按改动范围启动；C++ 构建不进入 PR CI，由提交前的本地验证承担。CI 流程见 `.github/workflows/ci.yml`。
- 里程碑发版 Tag 为 `v0.1-oculus` → `v0.2-discern` → `v0.3-nous` → `v1.0-vox`。`v*` Tag 触发全量测试、ARM64 构建和 Release 附件上传。发版 Commit 必须提供全部构建、测试和打包输入，包括 `deploy/edge-vision.service`。初始化阶段不打发版 Tag。


## 6. 跨 Agent 协作与交接
- Codex、DeepSeek 和 Claude Code 遵守同一契约；`CLAUDE.md` 仅作入口，不另立一份规范。
- 每次修改前先写清：目标仓库、业务模块 → 修改/新增的专属一级模块 → 用户功能结果、文件范围和验收方式。一级模块只归属一个业务模块，不允许跨业务复用或间接借用；依赖关系不代表归属。当前仅初始化技术目录，不宣称已完成业务模块划分或代码落地。
- 工作树开始先检查分支、完整 HEAD、远程和未提交变化。不得覆盖、自动丢弃其他 Agent 的工作；并发修改使用独立工作副本和互不重叠的文件范围，通过提交交接。
- 先在对应 Issue 记录范围及依赖；交接至少包含当前分支/提交、已改文件、验证结果、剩余问题和下一步。公开记录不含密码、令牌、真实私有影像或本机私人路径。
- 接收其他 Agent 的建议不扩大授权；部署、发消息、删除数据和公开额外资料须有对应用户授权。跨聊天通信先核对目的聊天及消息方向授权。
- GitHub main 只允许 PR + Squash 合并，启用自动删除源分支；禁止强推或删除 main，管理员也受保护。不通过临时关闭保护绕过流程。
- 报告 CI 时注明实际执行及跳过的任务。静态检查和空骨架门禁通过不能用来报告已构建、测试通过或完成发版。
- 同一主题仅维护一份正式依据：架构见 architecture，接口见 api/openapi，阶段交付与验收见 roadmap；变更同步关联文件。
- 默认简体中文、结论先行。状态区分已修改、已验证、已构建、已部署、线上已验收；记录日期使用北京时间。

## 7. CMake 模块化与依赖隔离（PATCH-005）

模块有实际源文件后，按 `CMakeLists.txt` 和 [架构构建契约](docs/architecture.md) 为各自目录建立 `add_library(... STATIC ...)` 目标。

| 静态库 | 允许的直接依赖 |
| --- | --- |
| `edge_core` | C++ 标准库、`Threads::Threads` |
| `edge_common` | `nlohmann_json::nlohmann_json` |
| `edge_capture` | `edge_core`、`edge_common`、FFmpeg、stb |
| `edge_detector` | `edge_core`、`edge_common`、NCNN |
| `edge_transport` | `edge_core`、`edge_common`、cpp-httplib、stb |
| `edge_streamer` | `edge_core`、`edge_common`、stb |
| `edge_api` | `edge_core`、`edge_common`、cpp-httplib |

- `src/main.cpp` 只装配所需模块，最终构建单个 `edge-vision-analyzer`。业务 `.cpp` 留在各自静态库目标。通过目标声明链接和 include 路径。FFmpeg 等底层运行库保持动态链接。
- 每个单测目标只直接链接 `edge_core`、`edge_common`、被测模块自身静态库和 `GTest::gtest_main`。被测模块可带入其自身依赖。例如 `test_detector` 不链接 FFmpeg、`edge_capture` 或主程序。
- 交叉编译设置 `BUILD_TESTING=OFF`。真实主程序目标提供 `install(TARGETS edge-vision-analyzer RUNTIME DESTINATION bin)` 供 Release 打包。根 CMake 文件已启用 C++17，并由 Issue #1 落地 `edge_core`、`test_build` 和 aarch64 工具链；其余静态库与运行目标随对应里程碑加入。
- 常规 PR 的 `ci-gate` 不运行 C++ 构建；C++ 变更必须在提交前本地通过 `cmake --build` 与 `ctest`。完整本机构建、CTest 与 aarch64 交叉编译由 `v*` Tag 的 Release 工作流执行，避免每个 PR 重复安装依赖。

## 8. Codex 与人工代码审查

每条评审评论用通顺的中文，最多 250 字。只报告内存泄漏、FFmpeg 或其他资源未释放、线程数据竞争或死锁、模块跨层违规依赖，以及 REST 或 SQLite 字段不匹配。说明问题发生的场景和后果。不要要求冗余的空指针或参数防御，也不要因无关文件的格式或个人命名偏好阻塞合并。

## 9. 依赖、入库与格式

- CMake 直接获取的第三方库必须指定明确的 Release Tag，禁止拉取 `main` 或 `master`。`cloud-storage/requirements.txt` 锁定主次版本，更新后验证离线用例。
- 构建目录、视频、模型文件、`cloud-storage/storage/` 和 `config/config.local.json` 必须被 Git 忽略。离线测试素材也遵守视频大文件屏蔽规则，可在测试运行时生成小样本。
- C++ 使用根 `.clang-format` 中的 Google 风格、4 空格缩进、100 列，格式化当前变更范围。

## 10. 硬件与实测台账（PATCH-011）

- [实测台账](docs/benchmarks.md) 只记录在对应真机、联想宿主机或实际手机源上核验的硬件、系统、有效配置和性能。未核验项一律填 `待实测`。设计目标、模板默认值和估算值不能当成实测值。
- 首次在魔百盒或联想 PC 上通过脚本取得真实的芯片、内存、系统和运行库信息时，补齐台账的对应设备行。手机源在实际接入后核对。部署脚本在原有系统和库版本之外，打印目标 CPU／SoC 型号和 `free -h`。
- 每个里程碑合入 `main` 后、打 Tag 前，使用 `/api/v1/stats` 和实际压测输出填写对应版本的一行，并把核心指标同步至 [README 性能摘要](README.md#性能摘要)。没测到的字段仍为 `待实测`。记录所测提交、工况、持续时间和输出来源，方便接力复跑。
- 对比优化收益时记录前后配置和相同的测试工况。前后数据都齐备后才能计算收益。
