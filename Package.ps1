$ErrorActionPreference='Stop'
$version='1.1.4'
$build=Join-Path $PSScriptRoot 'build\x64\Release'
$release=Join-Path $PSScriptRoot 'release'
$portable=Join-Path $release "CampusLink-$version-Portable-x64"
if(-not (Test-Path -LiteralPath (Join-Path $build 'CampusLink.exe'))){throw 'Build the app first.'}
if(Test-Path -LiteralPath $portable){throw 'The portable output already exists. Move it aside before creating a new release.'}
New-Item -ItemType Directory -Path $portable -Force | Out-Null
$exclude=@('.pdb','.lib','.exp','.ilk','.iobj','.ipdb','.log','.xml')
foreach($file in Get-ChildItem -LiteralPath $build -Recurse -File){
    if($file.Extension -in $exclude){continue}
    $relative=$file.FullName.Substring($build.Length).TrimStart('\')
    if($file.Name -in @('credential.xml','settings.json') -or $file.Extension -eq '.clixml'){throw 'Personal configuration must never be packaged.'}
    $target=Join-Path $portable $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '使用说明.txt') -Destination $portable
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '免责声明.txt') -Destination $portable
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD-PARTY-NOTICES.txt') -Destination $portable
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSE') -Destination (Join-Path $portable 'LICENSE.txt')
$licenseDirectory=Join-Path $portable 'Licenses'
New-Item -ItemType Directory -Path $licenseDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'installer\ChineseSimplified.LICENSE') -Destination (Join-Path $licenseDirectory 'ChineseSimplified.LICENSE.txt')
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'packages') -Directory -ErrorAction SilentlyContinue | Where-Object {$_.Name -like 'Microsoft.*'} | ForEach-Object {
    $package=$_
    Get-ChildItem -LiteralPath $package.FullName -File | Where-Object {$_.Name -match '(?i)license|notice'} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenseDirectory ($package.Name+'-'+$_.Name))}
}
# PackageReference restores into the NuGet cache, not necessarily ./packages.
# Copy notices only for the package versions used by this build.
$assetsPath=Join-Path $PSScriptRoot 'src\obj\project.assets.json'
if(Test-Path -LiteralPath $assetsPath){
    $restoreAssets=Get-Content -LiteralPath $assetsPath -Raw | ConvertFrom-Json
    foreach($library in $restoreAssets.libraries.PSObject.Properties){
        if($library.Value.type -ne 'package'){continue}
        foreach($folder in $restoreAssets.packageFolders.PSObject.Properties.Name){
            $packagePath=Join-Path $folder $library.Value.path
            if(-not (Test-Path -LiteralPath $packagePath)){continue}
            $prefix=$library.Name.Replace('/','-')
            Get-ChildItem -LiteralPath $packagePath -File | Where-Object {$_.Name -match '(?i)license|notice'} | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $licenseDirectory ($prefix+'-'+$_.Name))
            }
            break
        }
    }
}
$zip=Join-Path $release "CampusLink-$version-Portable-x64.zip"
Compress-Archive -LiteralPath $portable -DestinationPath $zip -CompressionLevel Optimal -Force
$iscc=Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
if(-not $iscc){
    foreach($candidate in @((Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),(Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'))){if(Test-Path -LiteralPath $candidate){$iscc=$candidate;break}}
}
if(-not $iscc){throw 'Inno Setup 6 is required to build the installer.'}
& $iscc (Join-Path $PSScriptRoot 'installer\CampusLink.iss')
if($LASTEXITCODE -ne 0){throw 'Installer build failed.'}
$files=Get-ChildItem -LiteralPath $release -File | Where-Object {$_.Extension -in @('.zip','.exe')}
$hashes=foreach($file in $files){$hash=Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256; "$($hash.Hash.ToLowerInvariant())  $($file.Name)"}
[IO.File]::WriteAllLines((Join-Path $release 'SHA256SUMS.txt'),$hashes,(New-Object Text.UTF8Encoding($false)))
$files | Select-Object Name,Length
