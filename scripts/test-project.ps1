$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$projectEngine=$engineSource
$projectCli=Join-Path $engineBin 'gmctl.exe'
$projectSample=Join-Path $projectEngine 'tests/fixtures/ui/empty.ui.json'
$projectFixture=Join-Path ([IO.Path]::GetTempPath()) ('GM 独立项目 '+[guid]::NewGuid().ToString('N'))
$projectChecks=[Collections.Generic.List[string]]::new()
$projectEngineHashes=@{};foreach($name in 'gm_editor','gm_player','gmctl'){$projectEngineHashes[$name]=(Get-FileHash "$engineBin/$name.exe").Hash}
function Project-Check($value,[string]$description){if(-not $value){throw "FAILED: $description"};$projectChecks.Add($description);Write-Host "PASS: $description"}
function Project-Send([string]$manifest,$requests){$responses=@($requests | ForEach-Object {$_|ConvertTo-Json -Depth 40 -Compress} | & $projectCli --project $manifest --stdio | ForEach-Object {$_|ConvertFrom-Json});foreach($r in $responses){if(-not $r.ok){throw ($r|ConvertTo-Json -Depth 8)}};return $responses}
$created=Project-Send $projectSample @(@{op='project.create';directory=$projectFixture;name='Independent Test'})
$projectManifest=$created.result.manifest
Project-Check (Test-Path -LiteralPath $projectManifest) 'Create project outside engine, with Unicode and spaces'
$initial=Project-Send $projectManifest @(@{op='project.query'})
Project-Check ($initial.result.components.Count -eq 9 -and $initial.result.systems.Count -eq 3) 'Precompiled template module registers without requiring a build'
$template=Project-Send $projectManifest @(@{op='preview.run'},@{op='runtime.step';frames=6;x=1},@{op='preview.stop'})
$originalX=$template[0].entities[0].x
Project-Check ([Math]::Abs($template[1].entities[0].x-$originalX-10) -lt .02) 'Template native System drives engine movement'
$scriptPath=Join-Path $projectFixture 'Scripts/game.cpp'
$scriptText=[IO.File]::ReadAllText($scriptPath)
$scriptText=$scriptText.Replace('namespace {',@'
namespace {
struct Clock {static constexpr const char* gm_name="game.Clock";int32_t frames=0;uint32_t enabled=1;};
const Field clockFields[]={GM_INT(Clock,frames,"Frames",0,0,2147483647),GM_BOOL(Clock,enabled,"Enabled",1)};
void tickClock(void*,const Context* c){c->each<Clock>([](Entity,Clock& clock){if(clock.enabled)++clock.frames;});}
'@)
$scriptText=$scriptText.Replace('component<Mover>("Mover",fields,1,required,2)}','component<Mover>("Mover",fields,1,required,2),component<Clock>("Clock",clockFields,2)}')
$scriptText=$scriptText.Replace('nullptr,nullptr,move,access,2}}','nullptr,nullptr,move,access,2},{"game.ClockTick","Clock tick",Phase::Late,nullptr,0,nullptr,nullptr,tickClock}}')
$scriptText=$scriptText.Replace('components,1,systems,1','components,2,systems,2')
[IO.File]::WriteAllText($scriptPath,$scriptText,[Text.UTF8Encoding]::new($false))
$extended=Project-Send $projectManifest @(@{op='project.build'},@{op='project.query'})
Project-Check ($extended[1].result.components.Count -eq 10 -and $extended[1].result.systems.Count -eq 4) 'New project component and System register without engine changes'
$scene=Get-Content (Join-Path $projectFixture 'assets/main.scene.json') -Raw | ConvertFrom-Json
$entityId=$scene.entities[0].id
$edits=Project-Send $projectManifest @(@{op='component.add';id=$entityId;type='game.Clock'},@{op='set';id=$entityId;property='game.Mover.speed';value=150},@{op='save'},@{op='preview.run'},@{op='runtime.step';frames=12;x=1},@{op='preview.stop'})
$clock=$edits[4].entities[0].components | Where-Object type -eq 'game.Clock'
Project-Check ($clock.fields.frames -eq 12 -and [Math]::Abs($edits[4].entities[0].x-$originalX-30) -lt .02) 'Namespaced component editing and custom System execute headlessly'
$toggle=Project-Send $projectManifest @(@{op='system.set';name='game.ClockTick';enabled=$false},@{op='preview.run'},@{op='runtime.step';frames=5},@{op='preview.stop'},@{op='undo'},@{op='project.reload'})
Project-Check (($toggle[2].entities[0].components|Where-Object type -eq 'game.Clock').fields.frames -eq 0 -and -not $toggle[4].dirty) 'System enablement is undoable and reload accepts restored schema'
$cycle=$scriptText.Replace('const System systems[]=', 'const char* cyclic[]={"game.ClockTick"}; const System systems[]=')
$cycle=$cycle.Replace('"Clock tick",Phase::Late,nullptr,0','"Clock tick",Phase::Late,cyclic,1')
[IO.File]::WriteAllText($scriptPath,$cycle,[Text.UTF8Encoding]::new($false))
$failed='{"op":"project.build"}' | & $projectCli --project $projectManifest | Out-String | ConvertFrom-Json
Project-Check (-not $failed.ok -and $failed.error -match 'cycle') 'Invalid System dependency cycle rejects module reload'
[IO.File]::WriteAllText($scriptPath,$scriptText,[Text.UTF8Encoding]::new($false))
$pack=Project-Send $projectManifest @(@{op='project.build'},@{op='project.package'})
Project-Check $pack[0].ok 'CLI rebuild recovers from previously invalid native DLL'
$dist=$pack[1].result.directory
$playerProcess=Start-Process -FilePath (Join-Path $dist 'gm_player.exe') -ArgumentList '--check' -WorkingDirectory $dist -WindowStyle Hidden -PassThru -Wait
Project-Check ($playerProcess.ExitCode -eq 0) 'Published game runs from its own binary-only directory'
Project-Check (-not (Get-ChildItem -LiteralPath $dist -Recurse -File|Where-Object Extension -In '.json','.cpp','.hpp','.png','.jpg')) 'Published package excludes source assets and scripts'
# Use the wrong module intentionally; scene/schema mismatch must fail before execution.
Copy-Item -LiteralPath (Join-Path $dist 'game.dll') -Destination (Join-Path $dist 'game-test-backup.dll')
Copy-Item -LiteralPath "$engineBuild/fixtures/Release/gm_test_module.dll" -Destination (Join-Path $dist 'game.dll') -Force
$invalidProcess=Start-Process -FilePath (Join-Path $dist 'gm_player.exe') -ArgumentList '--check' -WorkingDirectory $dist -WindowStyle Hidden -PassThru -Wait
Project-Check ($invalidProcess.ExitCode -ne 0) 'Player rejects scene/module schema mismatch'
Move-Item -LiteralPath (Join-Path $dist 'game-test-backup.dll') -Destination (Join-Path $dist 'game.dll') -Force
foreach($name in $projectEngineHashes.Keys){Project-Check ((Get-FileHash "$engineBin/$name.exe").Hash -eq $projectEngineHashes[$name]) "Extending project did not rebuild engine executable: $name"}
@{passed=$projectChecks.Count;checks=$projectChecks;fixture=$projectFixture;package=$dist}|ConvertTo-Json -Depth 8|Set-Content "$engineBuild/project-verification.json" -Encoding utf8
