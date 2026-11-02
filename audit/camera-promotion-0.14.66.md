# 0.14.66 视角冻结回归与推广入口屏蔽

用户实测0.14.65：皇家骑士推广仍显示，切换第三人称后视角冻结、切回恢复。用户澄清不是整个游戏进程卡死。尚未获得本次设备日志，不能确定唯一根因。

原实验路径在保留FPS主相机时调用SwitchToThirdPersonCharacter；反汇编确认该方法会SetActive(false)关闭原currentActiveCharacter对象，再激活另一个角色对象。这与“保留FPS相机/输入”的实验方案存在冲突。本版去除整组角色切换调用，不关闭fpsCharacter对象。通过System.Type.GetType解析Renderer，使用GameObject.GetComponentsInternal(type,true,true,true,false,null)收集渲染器；若根是Camera组件，先取GameObject。保留Renderer.enabled原值与弱引用，只隐藏FPS模型和武器相机下的渲染器，不停用其GameObject、Camera或输入/武器脚本。排除第三人称模型自己的渲染器，激活第三人称模型用于观察；开镜、切回、驾驶和离开游戏进行状态时还原保存状态。任一收集/属性调用失败则停止并还原，不尝试不匹配的泛型GetComponentsInChildren重载。

仍是实验性肩后显示，主相机偏移保持(+0.45,+0.15,-1.8)，逻辑FPS、准星与腰射链路保留。未声称已实测解决全部朝向、动画、准星、穿墙和武器显示问题；需要复测视角是否持续更新及两种视角射击。

推广处理：原先只封EnableBanner不足以覆盖直接LoadBannerDetails调用。本版同时检查并封住LoadBannerDetails原生入口，将场景PurchasedBannerAds组件12157的enabled设为false、GameObject998保持inactive。独立字节检查确认该组件只改变enabled一字节，其他组件字段不变。保留灰色关闭X和商店/其他奖励路径，不伪造皇家骑士发放成功。

保留真实黑底启动标志的中文替换及其他中文图片。编译、NRO/1874个资源文件哈希、独立NSP校验通过，实际冻结修复和启动弹窗禁用待实机验证。正式发布仍为0.14.58。
