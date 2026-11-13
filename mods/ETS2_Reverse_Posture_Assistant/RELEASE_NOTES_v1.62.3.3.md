# ETS2 Reverse Posture Assistant v1.62.3.3

本版面向 ETS2 1.62.3.3 Steam Windows x64（7173c707daa0）。原生运行组件校验可执行文件 SHA-256、四个渲染 Hook、退出命令 Hook 与四个模型生命周期辅助函数的签名。其他构建进入仅遥测模式。

修复倒车后 Alt+F4 退出卡住的问题。助手会在游戏处理退出命令前停止绘制并释放预测模型。实机验证了轨迹显示和正常退出。

完整包包含 .scs 和原生遥测 DLL。工坊用户需配套安装相同版本的 Runtime for Workshop。安装前请保存并退出游戏。

The native runtime targets ETS2 1.62.3.3 Steam Windows x64 (7173c707daa0). It checks the exact executable SHA-256, four render hook signatures, the quit-command hook signature, and four model-lifecycle helper signatures. Close the game before installing the Full package or the Workshop runtime.

Fixed an exit hang after using reverse guidance. The runtime releases prediction models before the game handles the quit command. Reverse guidance and Alt+F4 shutdown were verified in game.
