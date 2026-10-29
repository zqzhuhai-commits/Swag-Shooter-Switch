# 绝地精英 0.14.34-fix1

修复原生NSP在启动时提示 `Startup failed: process handle unavailable` 的问题。
实机诊断为 `rc=0x00010801 stage=1`：创建本地HIPC会话达到资源限制。改用Atmosphère/Mesosphere提供的接口直接取得真实进程句柄，保留失败检查。

## 使用方法

1. 关闭游戏，移除内存卡 `/atmosphere/exefs_patches/swag-startup-diag` 旧诊断/测试补丁目录。
2. 安装Release附件中的启动修复版NSP；遇到同版本提示时选择安装工具支持的覆盖或重装，保留存档。
3. 正常从主菜单启动。无需外部NRO或额外启动修复补丁。
4. 保留 `/switch/swag-userdata`，用于存档和运行时缓存。

## 版本与验证

- 标题ID：`01AF534741470000`；游戏/NSP显示版本：`0.14.34`；CNMT版本：34。
- 用户已确认功能修复后可进入游戏。发布NSP内置相同48字节修改；不是本次从全部源码重新构建的二进制。
- 校验包括NSO程序段、NCA分层哈希和内容ID、CNMT引用与摘要、成品文件回读；原游戏资源、NPDM和控制内容保持一致。
- 新Build ID避免此前诊断IPS再次匹配。新提供的APK资源尚未整合，本次只发布当前可玩基线。
- 仍需支持相应接口的Atmosphère/Mesosphere环境。未宣称所有玩法、存档恢复或其他系统版本均完成测试。

附件NSP SHA-256：

```text
5023d55045b999b524a1cd621618d424edf9ea467b3d85d890da5a0979c27f70
```
