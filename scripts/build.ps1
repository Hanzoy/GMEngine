param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
& "$PSScriptRoot/setup-deps.ps1"
& cmake -S $engineSource -B $engineBuild -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_INSTALL_PREFIX=$engineSdk"
if($LASTEXITCODE -ne 0){throw 'CMake configuration failed'}
& cmake --build $engineBuild --config $Configuration --parallel 8
if($LASTEXITCODE -ne 0){throw 'Build failed'}
& ctest --test-dir $engineBuild -C $Configuration --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Tests failed'}
& cmake --install $engineBuild --config $Configuration
if($LASTEXITCODE -ne 0){throw 'SDK installation failed'}
Write-Host "Engine SDK: $engineSdk"
