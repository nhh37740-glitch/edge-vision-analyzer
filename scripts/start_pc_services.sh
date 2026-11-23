#!/usr/bin/env bash
# PATCH-009 启动占位。本文件只登记流程，v0.3-nous 实现时总长不得超过 50 行。
# 在联想 PC 上启动两个独立进程：llama-server 监听 :8080，cloud-storage 监听 :8090。
# 使用本机模型和配置，依次请求 http://127.0.0.1:8080/health 和
# http://127.0.0.1:8090/health。两个探活都成功才报告就绪；任一失败返回非零状态。
