$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$fixture=Join-Path $engineBuildRoot ('sdk-portability-'+[guid]::NewGuid().ToString('N'))
$portable=Join-Path $fixture 'Engine Copy'
$game=Join-Path $fixture 'Game'
New-Item -ItemType Directory -Force $fixture|Out-Null
Copy-Item -LiteralPath $engineSdk -Destination $portable -Recurse
$cli=Join-Path $portable 'bin/gmctl.exe'
function Invoke-Sdk($project,$request){$result=$request|ConvertTo-Json -Depth 20 -Compress|& $cli --project $project|Out-String|ConvertFrom-Json;if(-not $result.ok){throw ($result|ConvertTo-Json -Depth 8)};return $result}
$created=Invoke-Sdk (Join-Path $portable 'editor/editor.ui.json') @{op='project.create';directory=$game;name='Portable SDK Test'}
$manifest=$created.result.manifest
Invoke-Sdk $manifest @{op='project.build'}|Out-Null
$cache=Get-Content -LiteralPath (Join-Path $game 'build/CMakeCache.txt') -Raw
$resolved=[regex]::Match($cache,'(?m)^GMEngine_DIR:[^=]+=(.+)$').Groups[1].Value.Trim()
if(-not $resolved -or [IO.Path]::GetFullPath($resolved) -ne [IO.Path]::GetFullPath((Join-Path $portable 'sdk/cmake'))){throw 'Game build did not resolve the relocated SDK'}
$package=Invoke-Sdk $manifest @{op='project.package'}
$check=Start-Process -FilePath $package.result.executable -ArgumentList '--check' -WorkingDirectory $package.result.directory -WindowStyle Hidden -PassThru -Wait
if($check.ExitCode -ne 0){throw 'Relocated SDK package failed'}
$session=Join-Path $fixture 'session'
$editor=Start-Process -FilePath (Join-Path $portable 'bin/gm_editor.exe') -ArgumentList @('--editor-shell','--session',('"'+$session+'"')) -WindowStyle Hidden -PassThru
try{
 $timer=[Diagnostics.Stopwatch]::StartNew();while(-not (Test-Path -LiteralPath "$session/session.json")){if($editor.HasExited -or $timer.Elapsed.TotalSeconds -gt 20){throw 'Relocated editor failed to start'};Start-Sleep -Milliseconds 80}
 $state=Get-Content -LiteralPath "$session/session.json" -Raw|ConvertFrom-Json
 if([IO.Path]::GetFullPath($state.project) -ne [IO.Path]::GetFullPath((Join-Path $portable 'editor/editor.ui.json'))){throw 'Editor did not use its installed resource copy'}
 $result='{"op":"query"}'|& $cli --live $session|Out-String|ConvertFrom-Json
 if(-not $result.ok){throw 'Relocated editor/CLI connection failed'}
}finally{if(-not $editor.HasExited){Stop-Process -Id $editor.Id}}
@{passed=5;fixture=$fixture;checks=@('SDK copied to a path containing spaces','Create project from relocated template','CMake uses relocated SDK include/configuration','Packaged Player works','Editor and CLI use relocated editor resources')}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $engineBuild 'sdk-portability-verification.json') -Encoding utf8
Write-Output 'PASS: relocated SDK creates, builds, packages, runs, and opens the editor without engine source paths'
