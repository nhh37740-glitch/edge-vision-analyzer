# ARM64 离线发行包

目标基线为 Ubuntu 24.04 noble／aarch64、glibc 2.39。包内保留动态链接，携带程序实际依赖的非 glibc 共享库；glibc 与 ELF 加载器由目标机提供。发行版、架构与库来源不能仅据文件名判断，运行验收记录见仓库 `docs/benchmarks.md`。

## 内容与加载方式

- `bin/edge-vision-analyzer`：ARM64 主程序，安装 RUNPATH 为 `$ORIGIN/../lib`。
- `lib/`：通过 ELF `DT_NEEDED` 递归收集的运行库，包含直接和间接依赖；每个库使用 `$ORIGIN` 查找同目录依赖。SONAME 对应的路径直接保存实际库文件，不依赖构建机绝对路径。
- `config/config.example.json`：公开配置模板。首次部署复制为 `config.local.json` 后填写现场参数；升级保留已有本地配置。
- `deploy/`：edge、journald、Fluent Bit 配置及采集器内存限制模板，不自动安装 Fluent Bit 软件包。
- `dependencies.tsv`：随包动态库对应的 Ubuntu 二进制包、版本、源包和源版本。
- `licenses/`：运行库及 NCNN、nlohmann/json、stb 的许可证／版权文本。库的许可证分别适用，不把整个依赖集合统称为 MIT。Ubuntu 对应源代码可按清单的源包与版本在 `https://launchpad.net/ubuntu/+source/<源包>/<源版本>` 获取；NCNN 使用仓库冻结的 Release Tag `20261020`。

模型、现场视频、私有配置、运行数据与凭据不在发行包中。目标机须已有 NCNN 模型和可用输入源。RTSP 输出还需要独立 FFmpeg CLI；当前盒端使用 `/usr/local/bin/ffmpeg` 静态版本，运行库闭包不包含该 CLI。

## 部署与回退

先在联想 PC 解压发行包。更新前将当前可执行文件和已有 `lib/` 拉回 PC 保存，盒端不保留回退副本；停止 edge 服务后安装新包 `lib/`，再在联想侧运行已有 `scripts/deploy_to_box.sh` 更新主程序。该脚本只上传程序与配置，**不会上传 `lib/`**，已有 `config.local.json` 默认保留。不要在服务仍运行时逐个覆盖共享库。验收后清理盒端上传的归档、临时 staging、安装包、源码和构建目录，只留下当前运行所需的文件。

在目标机启动前可直接读取加载器的解析结果，无需执行 x86 侧 `ldd`：

```sh
env -u LD_LIBRARY_PATH /lib/ld-linux-aarch64.so.1 --list \
  /opt/edge-vision-analyzer/bin/edge-vision-analyzer
```

随后确认服务 health、stats、快照与本地循环均正常。失败时先停止服务，从联想 PC 重新上传旧主程序和旧 `lib/`，保留配置与模型，再重新启动；库与程序必须作为同一版本一起恢复，盒端不另存副本。

## 构建与发布

普通 PR 只运行既有策略门禁，不运行 ARM 构建。`Release` 的手动入口执行 native 构建／离线测试、ARM64 构建与打包，上传 artifact 并跳过公开发布；`v*` Tag 才会在构建成功后创建 Release。手动构建成功只证明产物生成，离线目标机启动仍须单独验收。
