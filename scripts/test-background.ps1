param([switch]$KeepEditor)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$testRoot=Join-Path $engineBuildRoot ('sessions/background-'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $testRoot '游戏项目'
$session=Join-Path $testRoot 'session'
$cli=Join-Path $engineBin 'gmctl.exe'
New-Item -ItemType Directory -Force $testRoot | Out-Null
$previousUserData=$env:GM_ENGINE_USER_DATA
$env:GM_ENGINE_USER_DATA=Join-Path $testRoot 'user-data'
$checks=[Collections.Generic.List[string]]::new()
$latencies=[Collections.Generic.List[double]]::new()
function Check($ok,[string]$description){if(-not $ok){throw "FAILED: $description"};$checks.Add($description);Write-Host "PASS: $description"}
function Send($request){
    $file=Join-Path $testRoot 'request.json'
    [IO.File]::WriteAllText($file,($request|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
    $timer=[Diagnostics.Stopwatch]::StartNew()
    $response=& $cli --live $session --request $file | Out-String | ConvertFrom-Json
    if($request.op -eq 'query'){$latencies.Add($timer.Elapsed.TotalMilliseconds)}
    return $response
}
function Wait-Task {
    $timer=[Diagnostics.Stopwatch]::StartNew()
    do {
        $status=Send @{op='project.task.status'}
        if(-not $status.result.active){return $status.result}
        if($timer.Elapsed.TotalSeconds -gt 120){throw 'Background task timeout'}
        Start-Sleep -Milliseconds 100
    } while($true)
}
@{op='project.create';directory=$fixture;name='Background Test'}|ConvertTo-Json -Compress|& $cli --hub|Out-Null
if($LASTEXITCODE){throw 'Cannot create fixture'}
$manifest=Join-Path $fixture 'game.gmproject'
$cmake=Join-Path $fixture 'CMakeLists.txt'
$original=[IO.File]::ReadAllText($cmake)
# A controlled delay tests responsiveness/cancellation, not compilation throughput.
$delayed=$original+"`n"+'file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/delay-started" "ready")'+"`n"+'execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 30)'+"`n"
[IO.File]::WriteAllText($cmake,$delayed,[Text.UTF8Encoding]::new($false))
$process=Start-Process -FilePath (Join-Path $engineBin 'gm_editor.exe') -ArgumentList @('--project',('"'+$manifest+'"'),'--session',('"'+$session+'"')) -WindowStyle Hidden -PassThru
try {
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while(-not (Test-Path (Join-Path $session 'session.json'))){if($timer.Elapsed.TotalSeconds -gt 20 -or $process.HasExited){throw 'Editor did not start'};Start-Sleep -Milliseconds 50}
    $invalid=Send @{op='project.build';async='invalid'}
    Check (-not $invalid.ok -and -not (Send @{op='project.task.status'}).result.active) 'Malformed async option cannot start a task'
    $start=Send @{op='project.build';async=$true}
    Check ($start.ok -and $start.pending) 'Async build returns task ID immediately'
    $timer.Restart()
    while(-not (Test-Path (Join-Path $fixture 'delay-started'))){if($timer.Elapsed.TotalSeconds -gt 20){throw 'CMake delay did not start'};Start-Sleep -Milliseconds 50}
    1..5|ForEach-Object {Check (Send @{op='query'}).ok 'Query remains responsive during build'}
    Check (($latencies|Measure-Object -Maximum).Maximum -lt 3000) 'All queries finish within 3 seconds during a 30-second build delay'
    Check (Send @{op='view.inspector';page='systems'}).ok 'Inspector navigation remains available'
    Check (-not (Send @{op='set';id='player';property='name';value='Blocked'}).ok) 'Authoring mutations rejected during build'
    Check (-not (Send @{op='preview.run'}).ok) 'Simulation cannot race a project build'
    Check (-not (Send @{op='project.build';async=$true}).ok) 'Overlapping editor builds rejected'
    $competing='{"op":"project.assets"}'|& $cli --project $manifest|Out-String|ConvertFrom-Json
    Check (-not $competing.ok -and $competing.error -match 'busy') 'Independent CLI cannot write a project being built'
    Check (Send @{op='project.task.cancel'}).ok 'Cancellation accepted'
    $cancelled=Wait-Task
    Check ($cancelled.state -eq 'cancelled') 'Cancellation reaches a terminal state'
    [IO.File]::WriteAllText($cmake,$original,[Text.UTF8Encoding]::new($false))
    $built=Send @{op='project.build'}
    Check ($built.ok -and -not $built.pending -and (Test-Path (Join-Path $fixture 'build/game.gmscene'))) 'Default live CLI waits for successful build and recovers after cancellation'
    $package=Send @{op='project.package';async=$true}
    $packaged=Wait-Task
    Check ($package.pending -and $packaged.response.ok -and (Test-Path (Join-Path $fixture 'dist/gm_player.exe'))) 'Background packaging completes'
    $assets=Send @{op='project.assets'}
    Check ($assets.ok -and $assets.result.Count -gt 0) 'Live asset import retains final-response semantics'
    $scriptPath=Join-Path $fixture 'Scripts/game.cpp'
    $script=[IO.File]::ReadAllText($scriptPath)
    [IO.File]::WriteAllText($scriptPath,$script+"`n#error Expected_background_failure`n",[Text.UTF8Encoding]::new($false))
    $failed=Send @{op='project.build'}
    Check (-not $failed.ok -and $failed.error -match 'build failed') 'Build errors reach the original CLI caller'
    Check ((Send @{op='project.query'}).ok -and (Send @{op='preview.run';paused=$true}).ok) 'Failed build preserves the loaded module and usable scene'
    Check (Send @{op='preview.stop'}).ok 'Simulation still stops after a failed build'
    [IO.File]::WriteAllText($scriptPath,$script,[Text.UTF8Encoding]::new($false))
    $report=@{passed=$checks.Count;checks=$checks;queryMilliseconds=$latencies;fixture=$testRoot;editorPid=$process.Id}
    $report|ConvertTo-Json -Depth 8|Set-Content (Join-Path $engineBuild 'background-verification.json') -Encoding utf8
    Write-Output ($report|ConvertTo-Json -Depth 3)
} finally {
    if(-not $KeepEditor -and -not $process.HasExited){$process.CloseMainWindow()|Out-Null;if(-not $process.WaitForExit(10000)){Stop-Process -Id $process.Id}}
    $env:GM_ENGINE_USER_DATA=$previousUserData
}
