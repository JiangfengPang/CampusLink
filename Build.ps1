param([switch]$SkipTests, [switch]$Package)
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if(-not (Test-Path -LiteralPath $vswhere)){throw 'Install Visual Studio Build Tools 2022 with Desktop development with C++.'}
$installation=(& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(-not $installation){throw 'C++ build tools are not available yet.'}
$msbuild=Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'src\CampusLink.vcxproj') /restore /m:2 /nr:false /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
if($LASTEXITCODE -ne 0){throw 'WinUI build failed.'}
if(-not $SkipTests){
    & $msbuild (Join-Path $PSScriptRoot 'tests\CoreTests.vcxproj') /m:2 /nr:false /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
    if($LASTEXITCODE -ne 0){throw 'Test build failed.'}
    & (Join-Path $PSScriptRoot 'build\tests\CampusLink.Tests.exe')
    if($LASTEXITCODE -ne 0){throw 'Core tests failed.'}
}
if($Package){& (Join-Path $PSScriptRoot 'Package.ps1')}
