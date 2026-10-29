# Source, intermediate outputs, and installed SDK are separate directories.
$engineSource=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$engineBuildRoot=if($env:GM_ENGINE_BUILD_ROOT){[IO.Path]::GetFullPath($env:GM_ENGINE_BUILD_ROOT)}else{[IO.Path]::GetFullPath((Join-Path $engineSource '../GMEngineBuild'))}
$engineBuild=Join-Path $engineBuildRoot 'build'
$engineSdk=if($env:GM_ENGINE_ROOT){[IO.Path]::GetFullPath($env:GM_ENGINE_ROOT)}else{[IO.Path]::GetFullPath((Join-Path $engineSource '../GMEngineApp'))}
$engineBin=Join-Path $engineSdk 'bin'
