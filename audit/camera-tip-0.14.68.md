# 0.14.68 第三人称持枪姿态与加载提示署名

用户确认 0.14.67 首屏推广弹窗已经消失，保留该处理。
用户截图显示实际开火朝准星，模型和可见枪口偏向一侧；俯视显示全身是用户主动转镜头，不调整镜头距离。

## 修改
- 保持原 FPS 射击逻辑和镜头距离。
- 单独记录并启用本地第三人称模型的 aimIk，切回第一人称/瞄准/驾驶/结束时恢复原 enabled 值。
- 向 ThirdPersonAnimationController.OnVerticalRotationReceived 传入实际射击主镜头俯仰，并同步其 aimTargets 局部俯仰。
- 在该本地模型原生 LateUpdate 之后再次将根节点世界偏航同步到主镜头，避免动画更新覆盖轮询结果。其他角色原生 LateUpdate 保持原行为。
- 新增 hook 的四条原指令均为栈分配/寄存器保存，无 PC-relative 指令；原生入口四条字节比较后才安装 hook。
- 仅在 LoadingPanel.textToShowText 上替换开头 ASCII 署名加冒号，显示“小帆：”，保持提示正文和其他 UI 文本不变。按 UTF-16 转 UTF-8 保留中文和代理对。

## 验证
Switch 完整编译通过；NRO 1874 个嵌入文件逐项 SHA-256 校验通过；独立 NSP 验证见 nsp-verification-0.14.68.json。
新增 LateUpdate 原生入口指令与 guarded trampoline 检查通过。
没有实机运行；姿态/枪口对齐效果和提示展示待用户实测，不代表已验证解决。
