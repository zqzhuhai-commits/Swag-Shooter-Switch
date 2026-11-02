# 0.14.63 启动弹窗保护及视角切换实验

用户提供 0.14.61 截图：皇家骑士、四个属性条、LET YOUR SHOOTING MAKES SOUND. 与立即领取显示在白色面板。通过场景 Text 13365 追溯 Banner Item Ads Text → Banner Image → Purchased Banner Ads，确定为 PurchasedBannerAds 推广面板，不是 FirstDayBonus。设备日志有触摸交付和末尾约30 FPS，无原生异常记录；出现一次无调用栈的 NullReferenceException，不能仅凭它确定故障位置。

原生 OnClickedGrabItNow 在 _onGrabItNowAction 为空时直接返回。LoadBannerDetails 在加载资源和布局后才构造并赋予该委托；异常中断可能留下白色默认图像和无效领取按钮。新增保护只在该面板当前激活、中文物品名精确为皇家骑士且委托为空时调用原 OnCloseButtonClicked。此为启动阻塞的绕过保护，不是奖励发放修复，不记录虚假领取成功，也不清除存档或商品所有权。若委托有效则不跳过，需设备日志继续区分其他问题。

实验视角通过编译开关 SWAG_THIRD_PERSON_TEST 启用。步战长按L3半秒发出原子切换请求，短按释放后切换跑步。主线程使用原 GameManager.SetCameraMode，保留 checkSupport=true，weaponId=254（原生当前武器哨兵）；模式1为FPS、2为ThirdPerson。仅对存活本地角色和普通FPS/ThirdPerson模式提交；车辆和特殊镜头跳过。默认FPS，切换不写入原存档，失败时保留原视角。没有新建相机或改模型动画。原0.14.61安装包保留，GitHub正式版本未替换。

编译、NRO资源和NSP完整性校验通过。弹窗保护触发、领取问题、L3长短按、第三人称镜头/瞄准/载具效果均待实机验证。
