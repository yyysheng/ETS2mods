# ETS2 Reverse Posture Assistant v0.10.9

Restores the base-game trailer guide marker hidden by earlier hook-test code and model overrides. This release also includes the previously unpublished v0.10.8 build-profile compatibility hotfix.

## Changes
- Removes model/symbol/loading.pmd and loading.pmg overrides so the base game supplies its own marker and animation.
- Removes runtime writes to native marker visibility, position and rotation, including pause-time suppression.
- Preserves the independent blue tractor and orange trailer reverse boundaries.
- Keeps verified four-hook profiles and telemetry-only fallback for mismatched executables.
- Prevents stale native symbol assets from entering newly built mod archives.
- Skips unavailable Steam library paths during installation.

## Downloads and upgrade
- Full.zip: standalone mod plus DLL; run Install-Full.bat.
- Runtime_for_Workshop.zip: DLL installer for subscribers to https://steamcommunity.com/sharedfiles/filedetails/?id=3776052935 ; run Install-Runtime-Only.bat.
- Upgrade_from_v0.6.0.zip: migration installer for the old release; run Upgrade-From-v0.6.0.bat.

Exit ETS2 before installation. Update BOTH the mod content and the runtime DLL, then restart. Workshop users must let Steam update the item and separately install the new runtime. Standalone users should replace their old mod with the Full package. Disable older local copies and do not enable both standalone and Workshop copies.

## 中文
v0.10.9 修复此前 Hook 测试遗留导致的原游戏挂车指引框隐藏：移除 loading 模型覆盖，以及运行时对原生指引框显示、位置和旋转的改写，包括暂停时强制隐藏。蓝色车头与橙色挂车预测边界保持独立。

同时包含此前尚未公开发布的 v0.10.8 兼容性修复，保留四 Hook 构建校验和不匹配时的仅遥测降级。安装器会跳过失效 Steam 库路径。

升级前退出游戏，必须同时更新资源包和 DLL，完成后重新启动。工坊用户等待资源更新，并手动安装 Runtime_for_Workshop；独立安装使用 Full 包。停用旧版本地副本，不要同时启用工坊版和本地版。

## Validation
Release x64 runtime build, BuildProfileTests (including the installed ETS2 1.60.1.7 executable), KinematicsTests and package-content checks passed. The two loading-marker overrides are absent from the packaged mod. In-game visual acceptance remains pending. Supported range: verified ETS2 1.60.x Windows x64 layouts; no claim of 1.61 support.