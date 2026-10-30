# 0.14.52 提前结算与线程分配

用户报告：组队还有 32 人时宣布胜利；两个 CPU 核心闲置。

二进制检查确认 GameManager.OnPlayerDied (0x168855c) 仍扫描旧名单、写本地排名 1、调用 OnRoundLimitReachedMethod (0x1688150)。原结算调用 TeamManager.ShuMariTeamJitiGai (0x17548f0)，仅比较两个旧队伍生命。不能仅凭人数确定这次设备触发了哪条路径，因此本版同时拦截这些入口和通用 EndRoundTime (0x1694464)。

经典组队使用完整 52 人名册决定结算资格。我方全队淘汰可显示失败；否则只允许唯一存活队伍结束。不会把多队存活时的最大人数队伍当成胜者。其他模式回调保持原样。

主机测试提取并编译生产结算适配函数，使用模拟原回调检查提前结算、重复结算及其他模式回退。该测试不等于 Unity 事件生命周期与整局实机通过。

工作线程原 threadCreate 的 cpuid=-2；本机 libnx thread.h 定义 -2 为进程默认核心，而 NSP default_cpu_id=0。新策略读取进程 CoreMask，限制到应用核心 0..2，优先选 1/2 并设置可迁移掩码。失败不终止已创建线程。策略测试不证明实际调度成功或性能提升。

设备排查日志：SQUAD_END_BLOCKED、SQUAD_FINISH、SQUAD_ROSTER 和 GUEST_WORKER_AFFINITY。重点检查剩 32 人不结算、敌方最后一名死亡才胜利、我方团灭显示失败、再次开局正常，以及核心 1/2 是否获得负载。
