param([string]$MSBuildPath)
. "$PSScriptRoot\project-tools.ps1"
$project=Read-PluginProject
if(!$MSBuildPath) {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(Test-Path -LiteralPath $vswhere) {
        $MSBuildPath=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if(!$MSBuildPath -or !(Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
    throw 'Install Visual Studio 2022 or Build Tools with Desktop development with C++ (v143 and a Windows SDK), or pass -MSBuildPath.'
}
& $MSBuildPath (Join-Path $PSScriptRoot ($project.name+'.vcxproj')) /nologo /m /v:minimal /p:Configuration=Release /p:Platform=x64
if($LASTEXITCODE){throw 'Plugin build failed. Fix the first compiler error above.'}
$payload=Get-PluginPayload $project -Symbols
$dll=Join-Path $PSScriptRoot ('build\x64\Release\'+$project.name+'.dll')
Write-Output ('SHA256 '+(Get-PluginSha256 $dll)+'  '+$dll)
Write-Output 'Built Release x64. No game files changed. Keep the matching DLL/PDB pair for debugging.'
