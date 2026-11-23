# AGENTS.md — edge-vision-analyzer 开发契约

## 冷启动必读
在写代码前，按顺序阅读本文件、`docs/architecture.md`、`docs/api.md`、`docs/roadmap.md`、`config/config.json`；接口机器契约见 `docs/openapi.yaml`。这些仓库文件是接力依据，不依赖聊天记录。执行范围以用户最新指令为准。

## 0. 初始化与接力边界（PATCH-001 至 PATCH-005）
- PATCH-003 的无状态边缘与 PC 存储分工有效。PATCH-004/005 补充仓库协作模板、GitHub Actions 和构建契约。
- 初始化交付为文档、配置和骨架。C++ 与 Python 服务实现、依赖工具链、实际单测和部署留待后续开发。当前只核验工作流和文件结构，初始化门禁允许空模块通过。发版前必须具备可全量构建和测试的源文件与打包输入。
- 初始化文档和骨架通过绑定 Issue 的 PR 走 Squash 合并。合并后快进同步 `feature/capture`，核对远端 `main` 与该分支后停止。接手联想宿主机的 Agent 在收到后续开发指令后再实施业务功能。

## 1. 硬件拓扑、构建与存储约束
- **宿主机（开发 / 交叉编译 / 推理 / 存储）**：联想盒子电脑（2021 年奔腾 6 系列，x86_64 Linux，16GB DDR3）。负责 `aarch64` 交叉编译，独立运行 `llama-server`（`:8080`）和云存储模拟服务（`:8090`）。
- **边缘目标机（运行端）**：魔百盒（晶晨 Amlogic ARMv8 4 核 Cortex-A53，Armbian，2GB RAM）。严禁在魔百盒上直接编译大型 C++ 依赖。
- **资源硬红线**：魔百盒进程常驻 RSS 必须小于 150 MB。帧节点输入队列默认容量 2，满时丢弃最旧帧。异步事件队列容量 1，只保留最新待处理事件。
- **无状态边缘**：魔百盒仅保留实时处理所需的状态，历史事件与抓拍由联想 PC 持久化并查询。禁止在边缘实现历史事件环形表或失败帧落盘兜底。阶段 A 用户安排的取样截帧和 remux 录片属于显式验收动作，仍按路线图执行。
- **异步投递**：`GenAiWorker` 对干净抓拍只编码一次。开启 LLM 时先获取摘要，失败使用空摘要。开启存储时投递同一 JPEG 与结构化结果，超时或失败后结束本次任务，视频流水线继续处理。
- **云端事件留存**：`cloud-storage/` 单独维护 FastAPI、SQLite 和图片文件，与边缘编译无关。存储文件和抓拍只写联想 PC 的 Git 忽略目录，接口和 DDL 分别见 `docs/api.md` 与 `cloud-storage/schema.sql`。
- **物理网络**：手机经 Type-C 以太网扩展坞、魔百盒和联想 PC 各自接入同一以太网交换机，不以 USB 直连手机与魔百盒。DHCP / 离线静态地址方案见 `docs/architecture.md`。

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

## 4. 报错强制上云记录（GitHub Issue 规范）
在配置、交叉编译、链接、部署或运行测试过程中，**只要遇到任何配置错误、编译错误、链接失败或依赖缺失**：
1. 必须第一时间执行 `gh issue create --label "type:bug,agent-log"` 将问题记录到 GitHub，严禁静默绕过或只在终端日志里一笔带过。
2. 使用任务模板填写目标、文件范围、最多 5 条具体任务及可验收标准。在对应段落记录：
   - **复现命令**：触发报错的具体命令
   - **关键报错日志**：只贴最核心的 10~30 行，不贴全量刷屏日志
   - **根因判断**：一句话说明原因
   - **当前状态**：已修复（附 commit hash）/ 待人工处理
3. 修复通过一个 PR 交付，首行使用 `Fixes #<issue_id>`；合并时自动关闭这一个 Issue。

## 5. Git 与发版规范
- 一个 Issue 只对应一个模块的一项明确功能，或一个 `agent-log` 具体报错。使用 `.github/ISSUE_TEMPLATE/task.md` 写明涉及目录／文件和可验证的验收标准。禁止混入跨里程碑工作。实现按所列范围修改文件。
- 一个 Issue 对应一个 PR。使用 `.github/pull_request_template.md`，正文首行写 `Closes #<issue_id>` 或 `Fixes #<issue_id>`。只关联当前仓库一个打开的 Issue，不能附带关闭其他 Issue 或提交无对应 Issue 的 PR。
- **严禁直接向 `main` 分支提交或推送代码。**
- 分支命名：`feature/<module>`、`fix/<issue-id>`。
- 常规 C++ 开发提交前做两步验证：`cmake --build` 编译通过 + `ctest --output-on-failure` 通过。验证通过即可提 PR，不要重复多轮多余检查。
- `main` 的唯一必需状态检查是 `ci-gate`。所有指向 `main` 的 PR 均运行策略和改动识别，边缘及存储检查按改动范围启动。CI 流程见 `.github/workflows/ci.yml`。
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
| `edge_capture` | `edge_core`、`edge_common`、FFmpeg |
| `edge_detector` | `edge_core`、`edge_common`、NCNN |
| `edge_transport` | `edge_core`、`edge_common`、cpp-httplib、stb |
| `edge_streamer` | `edge_core`、`edge_common`、stb |
| `edge_api` | `edge_core`、`edge_common`、cpp-httplib |

- `src/main.cpp` 只装配所需模块，最终构建单个 `edge-vision-analyzer`。业务 `.cpp` 留在各自静态库目标。通过目标声明链接和 include 路径。FFmpeg 等底层运行库保持动态链接。
- 每个单测目标只直接链接 `edge_core`、`edge_common`、被测模块自身静态库和 `GTest::gtest_main`。被测模块可带入其自身依赖。例如 `test_detector` 不链接 FFmpeg、`edge_capture` 或主程序。
- 交叉编译设置 `BUILD_TESTING=OFF`。真实主程序目标提供 `install(TARGETS edge-vision-analyzer RUNTIME DESTINATION bin)` 供 Release 打包。当前根 CMake 文件只定义空工程和构建骨架。ARM64 工具链由后续 Issue #1 补齐。
