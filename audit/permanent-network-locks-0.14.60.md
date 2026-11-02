# 0.14.60 在线与私人服务器永久锁定

用户要求任何等级均不解锁。保留原 OnPlayOnlineButtonClicked 的禁用入口，并为 MainMenuManager.OnMultiplayerButtonPressAction、OnPrivateServerClicked、OnCreateServerPressed、OnJoinServerPressed 及 SelectGameMode.CreatePrivateMasterClient 增加受原始四条指令保护的直接返回补丁。判断不读取等级、购买状态或存档解锁标记。

运行中每四分之一秒恢复私人服务器和在线模式锁图标，禁用私人服务器、在线及创建按钮的交互属性（仅在实际对象支持该属性时设置）。按钮事件入口独立禁止，故等级刷新与 UI 动画不能恢复进入功能。离线组队继续保留。包含 0.14.59 受击停滞恢复。

编译、NRO 与 NSP 完整性校验通过。UI 图标显示、高等级存档及离线菜单交互仍待实机验证。本地测试版未替换 0.14.58 正式发布。
