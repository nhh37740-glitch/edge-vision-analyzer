#!/usr/bin/env bash
# PATCH-009 部署占位。本文件只登记流程，v1.0-vox 实现时总长不得超过 50 行。
# 从联想 PC 执行。输入目标魔百盒 SSH 地址、ARM64 产物和 config/config.local.json。
# 先取联想 PC 的 Unix 秒，通过 SSH 在魔百盒执行 sudo date -s "@<pc_epoch_seconds>"。
# 离线重启后若硬件时钟丢失，此步骤先恢复正确的系统时间。
# 部署前打印目标 /etc/os-release、ldd --version 和 FFmpeg 主版本；仅采集排障信息。
# scp 传输 ARM64 程序和本地配置，放到 edge-vision.service 约定的位置，再重启该服务。
# 重启后立即 curl -sf http://<box_ip>:8081/api/v1/health，并通过 SSH 输出最近 20 行
# journalctl -u edge-vision -n 20。启动或健康请求失败时仍输出日志，再返回非零状态。
