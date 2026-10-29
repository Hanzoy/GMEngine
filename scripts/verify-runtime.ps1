$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/paths.ps1"
$root=$engineSource
$build = $engineBuild
$playerProject = Get-Content (Join-Path $build 'gm_player.vcxproj') -Raw
$dependencies = [regex]::Matches($playerProject, '<AdditionalDependencies>(.*?)</AdditionalDependencies>') | ForEach-Object { $_.Groups[1].Value }
if (-not $dependencies -or ($dependencies -join ';') -match 'gm_core\.lib|gm_compiler\.lib|gm_ui\.lib|gm_image_import\.lib|nlohmann') { throw 'Player links an authoring dependency' }
$headers = Get-Content (Join-Path $build 'gm_player.dir/Release/gm_player.tlog/CL.read.1.tlog') -Encoding unicode
if (($headers -join "`n") -match 'nlohmann|stb_image\.h') { throw 'Player includes source parsing headers' }
$fixture = Join-Path $engineBuildRoot ('sessions/deployment-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$exe = Join-Path $fixture 'gm_player.exe'
$asset = Join-Path $fixture 'main.gmui'
Copy-Item -LiteralPath (Join-Path $build 'Release/gm_player.exe') -Destination $exe
Copy-Item -LiteralPath (Join-Path $build 'fixtures/Release/ui/counter.gmui') -Destination $asset
function Check-Exit([int]$expected) {
    $process = Start-Process -FilePath $exe -ArgumentList '--check' -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne $expected) { throw "Unexpected player check exit code: $($process.ExitCode)" }
}
Check-Exit 0
$bytes = [System.IO.File]::ReadAllBytes($asset)
$bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 128
[System.IO.File]::WriteAllBytes($asset,$bytes)
Check-Exit 1
Copy-Item -LiteralPath (Join-Path $root 'tests/fixtures/ui/counter.ui.json') -Destination $asset
Check-Exit 1
Copy-Item -LiteralPath (Join-Path $build 'fixtures/Release/ui/counter.gmui') -Destination $asset
$mainAsset = Join-Path $fixture 'main.gmui'
Copy-Item -LiteralPath (Join-Path $build 'fixtures/Release/ui/images.gmui') -Destination $mainAsset
Check-Exit 0
$pixels = [System.IO.File]::ReadAllBytes($mainAsset)
$pixels[$pixels.Length-1] = $pixels[$pixels.Length-1] -bxor 1
[System.IO.File]::WriteAllBytes($mainAsset,$pixels)
Check-Exit 1
Copy-Item -LiteralPath (Join-Path $build 'fixtures/Release/ui/images.gmui') -Destination $mainAsset
if (Get-ChildItem -LiteralPath $fixture -Recurse -File | Where-Object Extension -In '.json','.png','.jpg','.jpeg') { throw 'Source assets leaked into deployment' }
@{passed=8;fixture=$fixture;checks=@('No authoring libraries or image decoder linked','No JSON or decoder headers in Player','Runs from independent binary-only directory','Rejects corrupt resource','Rejects JSON input','Loads embedded image pixels','Rejects corrupt pixel data','No JSON/PNG/JPEG files in deployment')} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $build 'runtime-deployment-verification.json') -Encoding utf8
Write-Output 'PASS: binary-only deployment, embedded images, no JSON/image decoder dependency, invalid resource rejection'
