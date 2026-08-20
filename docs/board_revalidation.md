# RK3568 Revalidation Checklist

状态：`PENDING` — 开发板在 2026-08-20 离线，本清单不填假结果。

- [ ] 构建并部署当前 `audio_receiver`、模型和共享库
- [ ] 运行 public WAV，连续完成至少 10 个 YAMNet 窗口且无 crash
- [ ] 检查 `inference_ms`、`[PERF]` 汇总和温度保护日志
- [ ] 关闭第一个 PCM TCP client，确认 server 继续监听
- [ ] 重新连接第二个 client，确认新 session 不继承半个 sample/旧窗口
- [ ] 发送 `SIGINT` / Ctrl+C，确认 client、server、RKNN context 正常释放
- [ ] 重启进程并重复一次 public WAV 回归
- [ ] 用真实 K03S/K30S 麦克风完成 live microphone smoke
- [ ] 检查无 crash、死锁、假重连和异常残留进程
