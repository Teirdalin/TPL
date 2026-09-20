# Third-party notices

TPL's original code uses The Jelly Doughnut License (JDL-1), copyright (c)
2026 Teirdalin. See `LICENSE` and the complete terms in `Licenses/JDL-1.txt`.
`PLUGIN_API_PERMISSION.md` grants separate API/example reuse permission.
Plugin creators are free to choose JDL-1 or other terms for their own work;
TPL does not impose its license on independent plugins. Third-party components
retain the following notices and terms; JDL-1 does not replace them.

- MyGUI 3.2.2 headers: MIT, MyGUI Developers. Full notice is distributed as
  `Licenses/MyGUI.LICENSE.txt`. Source: https://github.com/MyGUI/mygui
  Pinned commit: `8a05127d7c5bb6772df88185b1f99d8052c379a4`.
- RapidJSON 1.1.0 headers: MIT and included third-party notices. Full notice is
  distributed as `Licenses/RapidJSON.LICENSE.txt`. Source: https://github.com/Tencent/rapidjson
  Pinned commit: `f54b0e47a08782a6131cc3d60f94d038fa6e0a51`.
  The complete upstream notice is retained, including its dependency notices.
  TPL uses headers, not RapidJSON's `bin/jsonchecker` code or test binaries.
- Microsoft Visual C++ runtime and Windows system libraries retain their
  respective licenses. TPL does not redistribute the compiler or game DLLs.
- MinHook 1.3.4: BSD 2-Clause, Tsuda Kageyu, with included Hacker Disassembler
  Engine notices by Vyacheslav Patkov. Full upstream notice is distributed as
  `Licenses/MinHook.LICENSE.txt`. Source: https://github.com/TsudaKageyu/minhook
  Pinned commit: `c3fcafdc10146beb5919319d0683e44e3c30d537`.
  This derives from upstream MinHook, not KenshiLib's multihook fork. TPL's
  `patches/minhook-1.3.4-context-rollback.patch` changes queued hook application
  so suspended-thread addresses are updated only after a patch succeeds.
  The upstream checkout and full notices are retained unchanged; the reviewed
  patch is applied to a generated build copy and compiled into TPLLib.

RE_Kenshi source was consulted to identify its existing manifest and loading
behavior. TPL does not incorporate its implementation or KenshiLib headers,
RVA tables, import libraries, or DLLs. Compatibility is not a licensing change
for third-party plugins that already use those projects.

Manual distributions contain `LICENSE`, `PLUGIN_API_PERMISSION.md`, this index,
and the `Licenses` folder. `JDL-1.txt` is TPL's reusable license, not a dependency.
Runtime updates carry the same complete texts in `runtime.json` under `licenses`,
so the existing three-file update format also includes its licensing material.
