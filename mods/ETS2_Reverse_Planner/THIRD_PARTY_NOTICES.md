# Third-party notices

The diagnostic game-traffic signature and the Windows 1.60 Prism3D structure
layout used by this project are derived from `ETS2LA/plugin`, commit
`c925bd965a96c58b73eaea9ca033fe0f0dc623a7`.

Copyright (c) 2024 Dario Wouters. Licensed under the MIT License. The complete
license text is available at <https://github.com/ETS2LA/plugin/blob/main/LICENSE.md>.

No TruckLib or MapExporter source code is copied into this project.

The pinned runtime baseline in `src/upstream` is derived from ETS2 Reverse
Posture Assistant, also maintained in this repository under the MIT License.

The runtime uses the SCS SDK telemetry headers and MinHook. Their license
texts are included in `licenses/`.


## Distribution license clarification

The project LICENSE is a scope index. The historical MIT text for covered original code is retained in licenses/LICENSE.historical-MIT.txt. Third-party notices apply only to the incorporated components, not to the entire mod. No new proprietary restrictions are activated by this distribution.

- SCS SDK: Copyright (C) 2016 SCS Software; official SDK 1.14 MIT text in licenses/LICENSE.scs-sdk.txt. Source: https://download.eurotrucksimulator2.com/scs_sdk_1_14.zip .
- SCS telemetry wrapper provenance: Copyright (c) 2014 Hans; the preserved licenses/LICENSE.scs-telemetry.txt is from RenCloud/scs-sdk-plugin. It does not replace the SCS SDK license.
- MinHook v1.3.3 (9fbd087432700d73fc571118d6a9697a36443d88): Copyright (C) 2009-2017 Tsuda Kageyu. HDE32/HDE64: Copyright (c) 2008-2009 Vyacheslav Patkov. Complete notices and terms are retained in licenses/LICENSE.minhook.txt.
- Current native runtime does not require ReShade. A dependency directory name is not a runtime installation requirement.
- Game-owned compatibility definitions and imagery remain subject to their owners' rights.

- ETS2LA/plugin: Copyright (c) 2024 Dario Wouters; MIT from commit c925bd965a96c58b73eaea9ca033fe0f0dc623a7 is included as licenses/LICENSE.ets2la-plugin.txt.
