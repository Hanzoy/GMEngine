param([switch]$Headless)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$fixture=Join-Path $engineBuildRoot ('sessions/clean-install-'+[guid]::NewGuid().ToString('N'))
$project=Join-Path $fixture 'New Game'
$manifest=Join-Path $project 'game.gmproject'
$cli=Join-Path $engineBin 'gmctl.exe'
$checks=[Collections.Generic.List[string]]::new()
$previousPath=$env:PATH
$previousCmake=$env:GM_ENGINE_CMAKE
$previousData=$env:GM_ENGINE_USER_DATA
$editor=$null
function Check($condition,$message){if(-not $condition){throw $message};$checks.Add($message);Write-Host "PASS: $message"}
function Send($requests){
    $results=@($requests|ForEach-Object {$_|ConvertTo-Json -Depth 30 -Compress}|& $cli --project $manifest --stdio|ForEach-Object {$_|ConvertFrom-Json})
    foreach($result in $results){if(-not $result.ok){throw ($result|ConvertTo-Json -Depth 8)}}
    return $results
}
try {
    # Even a developer workstation must fail if the editor attempts to launch CMake here.
    $env:PATH=Join-Path $env:SystemRoot 'System32'
    $env:GM_ENGINE_CMAKE=Join-Path $fixture 'missing-cmake.exe'
    $env:GM_ENGINE_USER_DATA=Join-Path $fixture 'userdata'
    $created=@{op='project.create';directory=$project;name='No Toolchain Game'}|ConvertTo-Json -Compress|& $cli --hub|Out-String|ConvertFrom-Json
    Check ($created.ok -and (Test-Path -LiteralPath $manifest)) 'Create a project without CMake on PATH'
    $data=Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json
    $module=Join-Path $project $data.module
    Check ((Test-Path -LiteralPath $module) -and $data.module -eq 'bin/windows-x64/Game.dll') 'New project contains its ready-to-run native module'
    $states=Send @(@{op='project.query'},@{op='set';id='player';property='game.Mover.speed';value=150},@{op='save'},@{op='preview.run'},@{op='runtime.step';frames=6;x=1},@{op='preview.stop'},@{op='project.package'})
    Check ($states[0].result.components.Count -eq 9 -and [Math]::Abs($states[4].entities[0].x-$states[3].entities[0].x-15) -lt .02) 'Edit, save and simulate the template without invoking a compiler'
    Check (-not (Test-Path -LiteralPath "$project/build/CMakeCache.txt")) 'Opening and running do not configure a native build'
    $player=Start-Process -FilePath $states[6].result.executable -ArgumentList '--check' -WorkingDirectory $states[6].result.directory -WindowStyle Hidden -Wait -PassThru
    Check ($player.ExitCode -eq 0) 'Package and validate the game without CMake or Visual Studio on PATH'
    $failed='{"op":"project.build"}'|& $cli --project $manifest|Out-String|ConvertFrom-Json
    Check (-not $failed.ok -and $failed.error -match 'GM_ENGINE_CMAKE') 'Explicit native build reports the configured missing build tool'
    Check ((Send @(@{op='project.query'}))[0].ok) 'Failed native build preserves the usable project module'
    if(-not $Headless){
        $session=Join-Path $fixture 'editor-session'
        $editor=Start-Process -FilePath "$engineBin/gm_editor.exe" -ArgumentList @('--project',('"'+$manifest+'"'),'--session',('"'+$session+'"')) -WindowStyle Hidden -PassThru
        $timer=[Diagnostics.Stopwatch]::StartNew()
        while(-not (Test-Path -LiteralPath "$session/session.json")){
            if($editor.HasExited -or $timer.Elapsed.TotalSeconds -gt 20){throw 'Editor could not open the precompiled project'}
            Start-Sleep -Milliseconds 80
        }
        $live='{"op":"preview.run","paused":true}'|& $cli --live $session|Out-String|ConvertFrom-Json
        $state='{"op":"runtime.query"}'|& $cli --live $session|Out-String|ConvertFrom-Json
        Check ($live.ok -and $state.ok -and $state.entities.Count -eq 1 -and $state.paused) 'Installed GUI opens and runs a new project with no build tool available'
        '{"op":"preview.stop"}'|& $cli --live $session|Out-Null
        Stop-Process -Id $editor.Id
        $editor=$null
    }
    # Recover a starter project left behind by 0.8.0's failed first-build path.
    $legacy="cmake_minimum_required(VERSION 3.21)`nproject(Game LANGUAGES CXX)`nfind_package(GMEngine CONFIG REQUIRED)`ngm_add_game_module(Game Scripts/game.cpp)`n"
    [IO.File]::WriteAllText((Join-Path $project 'CMakeLists.txt'),$legacy)
    $data.module='build/Release/Game.dll'
    $data|ConvertTo-Json|Set-Content -LiteralPath $manifest -Encoding utf8
    Send @(@{op='project.query'})|Out-Null
    $legacyModule=[IO.Path]::GetFullPath((Join-Path $project $data.module))
    Check ((Test-Path -LiteralPath $legacyModule) -and -not (Test-Path -LiteralPath "$project/build/CMakeCache.txt")) 'An unchanged 0.8.0 starter project recovers without recompiling'
    # Changed native sources must never be replaced with the starter game's behavior.
    $projectRoot=[IO.Path]::GetFullPath($project)
    $backup=[IO.Path]::GetFullPath($legacyModule+'.backup')
    if(-not $legacyModule.StartsWith($projectRoot+'\') -or -not $backup.StartsWith($projectRoot+'\')){throw 'Test module path escapes fixture'}
    Move-Item -LiteralPath $legacyModule -Destination $backup
    Add-Content -LiteralPath "$project/Scripts/game.cpp" -Value '// User-modified game source'
    $rejected='{"op":"project.query"}'|& $cli --project $manifest|Out-String|ConvertFrom-Json
    Check (-not $rejected.ok -and $rejected.error -match 'GM_ENGINE_CMAKE' -and -not (Test-Path -LiteralPath $legacyModule)) 'Custom game code is never silently replaced by the precompiled starter'
    @{passed=$checks.Count;checks=$checks;fixture=$fixture}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$engineBuild/installation-verification.json" -Encoding utf8
} finally {
    if($editor -and -not $editor.HasExited){Stop-Process -Id $editor.Id}
    $env:PATH=$previousPath
    $env:GM_ENGINE_CMAKE=$previousCmake
    $env:GM_ENGINE_USER_DATA=$previousData
}
