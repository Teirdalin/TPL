# TPL developer kit 0.1.1

Start with [Your first plugin](docs/developers/QUICKSTART.md), or open the
[full developer guide](docs/developers/README.md).

This kit creates independent plugin projects for Visual Studio 2022/v143.
It contains headers, a project generator, build/deploy/package scripts,
compilable examples and API documentation. No loader/game binaries, compiler,
import libraries or private tests are bundled.

For existing projects, `tools/convert-rekenshi-project.py` creates a bounded,
non-destructive migration inventory, plan and clean TPL starter. Read
`docs/developers/REKENSHI_TO_TPL_COMPENDIUM.md` for the complete migration
workflow and the native ABI review boundary.

```powershell
./tools/New-TPLPlugin.ps1 -Name MyFirstMod -Destination C:\KenshiMods\MyFirstMod
cd C:\KenshiMods\MyFirstMod
./build.ps1
```

Install [TPL](https://github.com/Teirdalin/TPL/releases/tag/v0.1.1) separately
before deploying to the game. The modern starter uses only the public C API;
raw native game C++ bindings still require their reviewed legacy ABI. This
kit does not claim full KenshiLib functionality or automatic plugin conversion.

Your plugin may use its own license and stay closed-source. Keep the notices
required by [the API/template permission](PLUGIN_API_PERMISSION.md) for the
TPL portions. [JDL-1](LICENSE) does not automatically license your own work.
The official optional JDL logo is included at `Licenses/JDL.png` for developers
who want to use it with JDL-licensed work.
