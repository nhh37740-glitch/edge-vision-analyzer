# 魔百盒 CAVIAR 取流截图

这两张图是 2026-11-24 魔百盒取流验收时，从盒端 `/api/v1/snapshot.jpg`（MJPEG 快照，OSD 已绘制检测框）保存的画面，源为公开数据集 CAVIAR 序列 `OneStopEnter1cor` 经联想 PC 中继推流。验收期间把 MAD 阈值临时改为 0.0 以强制检测，结束后恢复 8.0；配置与模型不入库。

| 文件 | 说明 |
| --- | --- |
| `osd-sheet-2026-11-24.jpg` | 6 张单帧快照拼成的 2×3 展示图（每帧 640×480），可见 `person 0.94`、`0.92`、`0.91` 等框 |
| `osd-frame-2026-11-24.jpg` | 单帧原图 640×480，可见 `person 0.64` 框 |

拼图仅由 `ffmpeg` 拼接，未改动画面内容；检测框与分数由项目内 NCNN／YOLO-Fastest-v2 实时绘制，不是 CAVIAR 官方标注。

图像来源：**EC Funded CAVIAR project/IST 2001 37540**，[数据集来源页](http://homepages.inf.ed.ac.uk/rbf/CAVIAR/)，标注 **CC BY-SA**（未注明版本）。上述图像经过抽帧、缩放、编码与检测框绘制，沿用原许可。未使用现场私有画面、凭据或私有配置。
