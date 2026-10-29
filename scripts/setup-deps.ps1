$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root 'third_party'
New-Item -ItemType Directory -Force -Path $deps | Out-Null
$pins = @(
    @{ Name = 'bgfx.cmake'; Owner = 'bkaradzic'; Rev = '5ea74ee66662daef4089695846d6322da0dde94e' },
    @{ Name = 'bx'; Owner = 'bkaradzic'; Rev = '0e5bbca6878000f6371c65c53fe06f6f0ae87ca9' },
    @{ Name = 'bimg'; Owner = 'bkaradzic'; Rev = '9bfbd6493b48a15238f2d1374a718ec026524515' },
    @{ Name = 'bgfx'; Owner = 'bkaradzic'; Rev = 'fe9bbdec66110f8626b9251a4947fc336ac3f62c' }
)
foreach ($pin in $pins) {
    $destination = Join-Path $deps $pin.Name
    if (Test-Path -LiteralPath $destination) { continue }
    Write-Host "Downloading $($pin.Name) at $($pin.Rev)"
    $archive = Join-Path $deps "$($pin.Name).zip"
    Invoke-WebRequest -Uri "https://codeload.github.com/$($pin.Owner)/$($pin.Name)/zip/$($pin.Rev)" -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath $deps
    Move-Item -LiteralPath (Join-Path $deps "$($pin.Name)-$($pin.Rev)") -Destination $destination
}
$jsonDir = Join-Path $deps 'nlohmann'
New-Item -ItemType Directory -Force -Path $jsonDir | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $jsonDir 'json.hpp'))) {
    Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp' -OutFile (Join-Path $jsonDir 'json.hpp')
    Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT' -OutFile (Join-Path $jsonDir 'LICENSE.MIT')
}
Write-Host 'Pinned dependencies are ready.'
& "$PSScriptRoot/setup-ecs.ps1"
