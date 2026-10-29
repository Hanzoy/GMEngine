param([string]$Output)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$ecsRoot=$engineSource
if (-not $Output) { $Output=Join-Path $engineBuild 'benchmarks/ecs' }
New-Item -ItemType Directory -Force $Output | Out-Null
& "$PSScriptRoot/setup-ecs.ps1"
cmake -S $ecsRoot -B $engineBuild -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
cmake --build $engineBuild --config Release --target gm_ecs_bench --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Benchmark build failed' }
foreach ($ecsCount in @(1000,10000,100000)) {
    foreach ($ecsBackend in @('flecs','entt')) {
        & "$engineBuild/Release/gm_ecs_bench.exe" $ecsBackend $ecsCount 1> "$Output/$ecsBackend-$ecsCount.csv" 2> "$Output/$ecsBackend-$ecsCount-memory.txt"
        if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $ecsBackend $ecsCount" }
    }
}
Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors | ConvertTo-Json | Set-Content "$Output/machine.json" -Encoding utf8
Write-Host "Flecs 4.1.6 / EnTT 3.16.0 results: $Output"
