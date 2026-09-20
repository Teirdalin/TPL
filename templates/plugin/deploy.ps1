param([Parameter(Mandatory=$true)][string]$GameDir,[switch]$Replace)
. "$PSScriptRoot\project-tools.ps1"
$project=Read-PluginProject
$files=Get-PluginPayload $project -Symbols
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path.TrimEnd('\')
$homePath=Join-Path $GameDir 'TPL'
if(!(Test-Path -LiteralPath (Join-Path $homePath 'current.txt'))){throw 'Install TPL in this Kenshi folder first.'}
function Assert-Closed {
    if(Get-Process -Name kenshi_x64,kenshi_GOG_x64 -ErrorAction SilentlyContinue){throw 'Close Kenshi before deploying; DLLs cannot be hot-reloaded.'}
}
Assert-Closed
$target=[IO.Path]::GetFullPath((Join-Path $homePath ('plugins\'+$project.name)))
if(!$target.StartsWith($homePath+'\plugins\',[StringComparison]::OrdinalIgnoreCase)){throw 'Deployment path escaped TPL/plugins.'}
foreach($path in @($GameDir,$homePath,(Join-Path $homePath 'plugins'),$target)) {
    if((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Refusing redirected deployment folders.'}
}
if(Test-Path -LiteralPath $target) {
    $pending=New-Object 'System.Collections.Generic.Queue[string]'
    $pending.Enqueue($target)
    while($pending.Count) {
        foreach($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Refusing redirected files or folders inside the installed plugin.'}
            if($item.PSIsContainer){$pending.Enqueue($item.FullName)}
        }
    }
    if(!$Replace){throw 'Plugin folder exists. Use -Replace to retain a backup and update it.'}
    $backup=Join-Path $PSScriptRoot ('backups\'+$project.name+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
    Copy-Item -LiteralPath $target -Destination $backup -Recurse
    Write-Output "Previous plugin retained: $backup"
}
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach($path in $files.Keys) {
    Assert-Closed
    $dest=Join-Path $target $files[$path]
    if($files[$path] -eq 'plugin.cfg' -and (Test-Path -LiteralPath $dest)){continue}
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
    Copy-Item -LiteralPath $path -Destination $dest -Force
    if((Get-FileHash -LiteralPath $path).Hash -ne (Get-FileHash -LiteralPath $dest).Hash){throw 'Deployed file checksum mismatch.'}
}
Write-Output "Deployed to $target. Config preserved; saves, mod order and loader settings unchanged. Restart Kenshi to test."
