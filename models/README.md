# 检测模型

本目录只保存说明文件，模型权重不入库。`.gitignore` 已忽略 `*.param`、`*.bin`、`*.onnx`、`*.gguf` 等模型文件；单元测试不会下载权重，也不会加载模型。

默认配置 [config.example.json](../config/config.example.json) 使用 YOLO-Fastest-v2：

```json
"model_param": "models/yolo-fastest-v2.param",
"model_bin": "models/yolo-fastest-v2.bin",
"input_size": 320,
"conf_threshold": 0.5,
"target_labels": ["person"]
```

路径相对进程工作目录。部署时把权重放在仓库根目录的 `models/`，或修改本地 `config/config.local.json` 指向实际位置。

## YOLO-Fastest-v2（默认）

1. 从 [dog-qiuqiu/Yolo-FastestV2](https://github.com/dog-qiuqiu/Yolo-FastestV2) 获取 COCO 模型，或按该仓库说明由 `.pth` 转 ONNX，再用 ncnn 的 `onnx2ncnn` 和 `ncnnoptimize` 生成 `.param` / `.bin`。仓库的 `sample/ncnn/model/` 也提供转换后的 `yolo-fastestv2-opt.param`、`yolo-fastestv2-opt.bin`。
2. 复制并重命名为 `models/yolo-fastest-v2.param` 与 `models/yolo-fastest-v2.bin`。
3. 该模型输入为 RGB 三通道、归一化到 `[0,1]`。`NcnnDetectorNode` 会先做 320×320 letterbox 预处理，输出按 YOLO-Fastest-v2 的 stride 16/32 特征图解码为归一化 `TargetBox`，再按 `target_labels`、`conf_threshold` 过滤并执行类别感知 NMS。
4. 若转换后的输出层名称不是默认的 `794`、`796`，节点会从模型声明的输出层自动读取；类别名称按 COCO-80 顺序解释。

## NanoDet-Plus

1. 从 [RangiLyu/nanodet](https://github.com/RangiLyu/nanodet) 的模型仓库获取 NanoDet-Plus 的 `.pth`，按 `demo_ncnn` 说明转成 ncnn `.param` / `.bin`。
2. 复制为 `models/nanodet-plus.param` 与 `models/nanodet-plus.bin`，并在本地配置中修改 `detector.model_param` / `detector.model_bin`。
3. NanoDet-Plus 的后处理（中心先验、`reg_max` 与类别数）与 YOLO-Fastest-v2 不同。当前 `NcnnDetectorNode` 内置解码针对 YOLO-Fastest-v2 原始输出格式，直接替换 NanoDet-Plus 权重不会得到正确框；接入 NanoDet-Plus 前需补对应解码分支。本阶段先把权重与放置方式登记在此，不把不兼容模型当作已验证能力。

## 许可与体积

- YOLO-Fastest-v2 与 NanoDet-Plus 的许可和再分发条件以其上游仓库为准；本项目不转售或再分发权重。
- 权重文件体积较大且随训练数据变化，不提交到 Git；`git status` 不应出现 `models/*.param` 或 `models/*.bin`。
- 测试与 CI 只使用合成数据，不访问模型下载地址。
