param([string]$GameDir='E:\SteamLibrary\steamapps\common\Kenshi',[string]$Python='python.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
& $Python "$PSScriptRoot\generate-engine.py" --game (Join-Path $GameDir 'kenshi_x64.exe') --recipe "$root\bindings\steam-1.0.65-save-ui.json" --output "$root\build\engine"
if($LASTEXITCODE){throw 'Native binding discovery failed; do not use an older generated profile.'}
$extra=@()
if($env:TPL_PARITY_INDEX){$extra=@('--index',$env:TPL_PARITY_INDEX)}
& $Python "$PSScriptRoot\parity_bindings.py" --game (Join-Path $GameDir 'kenshi_x64.exe') --base "$root\build\engine\bindings.json" --recipes "$root\bindings\reviewed" --output "$root\build\engine" @extra
if($LASTEXITCODE){throw 'Reviewed binding expansion failed; do not compile an older profile.'}
