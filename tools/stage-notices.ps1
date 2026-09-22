$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$dist=Join-Path $root 'dist'
$runtime=Join-Path $dist 'TPL\versions\0.1.3'
$licenses=Join-Path $dist 'Licenses'
New-Item -ItemType Directory -Force -Path $runtime,$licenses | Out-Null
foreach($name in @('LICENSE','PLUGIN_API_PERMISSION.md','THIRD_PARTY_NOTICES.md','README.md')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $dist -Force
}
foreach($name in @('JDL-1.txt','MyGUI.LICENSE.txt','RapidJSON.LICENSE.txt','MinHook.LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $root ('Licenses\'+$name)) -Destination $licenses -Force
}
$texts=[ordered]@{}
foreach($name in @('LICENSE','PLUGIN_API_PERMISSION.md','Licenses/JDL-1.txt','THIRD_PARTY_NOTICES.md','Licenses/MyGUI.LICENSE.txt','Licenses/RapidJSON.LICENSE.txt','Licenses/MinHook.LICENSE.txt')) {
    $texts[$name]=[IO.File]::ReadAllText((Join-Path $root $name))
}
$manifest=[ordered]@{version='0.1.3';bootstrapAbi=1;licenses=$texts}
[IO.File]::WriteAllText((Join-Path $runtime 'runtime.json'),($manifest | ConvertTo-Json -Depth 4),(New-Object Text.UTF8Encoding($false)))
