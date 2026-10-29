$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$fixture=Join-Path $engineBuildRoot ('sessions/v08-'+[guid]::NewGuid().ToString('N'))
$previousUserData=$env:GM_ENGINE_USER_DATA
try {
$env:GM_ENGINE_USER_DATA=Join-Path $fixture 'userdata'
$project=Join-Path $fixture 'Game'
$manifest=Join-Path $project 'game.gmproject'
$cli=Join-Path $engineBin 'gmctl.exe'
$checks=[Collections.Generic.List[string]]::new()
function Check($condition,$message){if(-not $condition){throw $message};$checks.Add($message);Write-Host "PASS: $message"}
function Send($requests){$results=@($requests|ForEach-Object {$_|ConvertTo-Json -Depth 40 -Compress}|& $cli --project $manifest --stdio|ForEach-Object {$_|ConvertFrom-Json});foreach($r in $results){if(-not $r.ok){throw ($r|ConvertTo-Json -Depth 10)}};return $results}
$created=@{op='project.create';directory=$project;name='V08 Contract'}|ConvertTo-Json -Compress|& $cli --hub|Out-String|ConvertFrom-Json
Check ($created.ok -and (Test-Path -LiteralPath $manifest)) 'Create a project without opening an existing document'
$added=@{op='project.add';path=$manifest}|ConvertTo-Json -Compress|& $cli --hub|Out-String|ConvertFrom-Json
$recent='{"op":"project.recent"}'|& $cli --hub|Out-String|ConvertFrom-Json
Check ($added.ok -and $recent.ok -and $recent.result.Count -eq 1 -and $recent.result[0].name -eq 'V08 Contract' -and $recent.result[0].exists) 'Recent project persists across launcher processes'
$added=@{op='project.add';path=$manifest}|ConvertTo-Json -Compress|& $cli --hub|Out-String|ConvertFrom-Json
Check ($added.ok -and $added.result.Count -eq 1) 'Opening an existing recent project does not duplicate it'
$forgot=@{op='project.forget';path=$manifest}|ConvertTo-Json -Compress|& $cli --hub|Out-String|ConvertFrom-Json
$recent='{"op":"project.recent"}'|& $cli --hub|Out-String|ConvertFrom-Json
Check ($forgot.ok -and $recent.result.Count -eq 0 -and (Test-Path -LiteralPath $manifest)) 'Forgetting a recent project preserves its files'
$second=Get-Content -LiteralPath "$project/assets/main.scene.json" -Raw|ConvertFrom-Json
$second.entities[0].components[0].fields.x=600
$second|ConvertTo-Json -Depth 30|Set-Content -LiteralPath "$project/assets/second.scene.json" -Encoding utf8
Copy-Item -LiteralPath "$engineSource/tests/fixtures/images/rgba.png" -Destination "$project/assets/image.png"
$indexed=Send @(@{op='project.assets'})
$imageId=($indexed.result|Where-Object path -eq 'assets/image.png').id
$hud=Get-Content -LiteralPath "$engineSource/tests/fixtures/ui/empty.ui.json" -Raw|ConvertFrom-Json
$hud.root.w=1280;$hud.root.h=720
$hud.root.children=@(@{id='picture';name='Picture';type='image';x=10;y=10;w=64;h=64;source="asset://$imageId"})
$hud|ConvertTo-Json -Depth 20|Set-Content -LiteralPath "$project/assets/hud.ui.json" -Encoding utf8
$indexed=Send @(@{op='project.assets'})
$hudId=($indexed.result|Where-Object path -eq 'assets/hud.ui.json').id
$main=Get-Content -LiteralPath "$project/assets/main.scene.json" -Raw|ConvertFrom-Json
$main|Add-Member -NotePropertyName ui -NotePropertyValue @{source="asset://$hudId";bindings=@()}
$main|ConvertTo-Json -Depth 30|Set-Content -LiteralPath "$project/assets/main.scene.json" -Encoding utf8
Send @(@{op='project.build'})|Out-Null
$first=Get-Content -LiteralPath "$project/build/import-report.json" -Raw|ConvertFrom-Json
Check ($first.Count -eq 2 -and @($first|Where-Object reused).Count -eq 0) 'Import all project scenes with stable resource IDs'
Send @(@{op='project.build'})|Out-Null
$secondReport=Get-Content -LiteralPath "$project/build/import-report.json" -Raw|ConvertFrom-Json
Check (@($secondReport|Where-Object reused).Count -eq 2) 'Unchanged scene dependencies reuse compiled binaries'
$assetRoot=[IO.Path]::GetFullPath("$project/assets")
foreach($name in 'image.png','image.png.gmmeta'){$from=[IO.Path]::GetFullPath((Join-Path $assetRoot $name));$to=[IO.Path]::GetFullPath((Join-Path $assetRoot ('renamed'+$name.Substring(5))));if(-not $from.StartsWith($assetRoot+'\') -or -not $to.StartsWith($assetRoot+'\')){throw 'Test asset path escapes fixture'};Move-Item -LiteralPath $from -Destination $to}
Send @(@{op='project.build'})|Out-Null
$indexed=Send @(@{op='project.assets'})
Check (($indexed.result|Where-Object path -eq 'assets/renamed.png').id -eq $imageId) 'Stable URI survives moving the image and its metadata'
# Both formats are supported by the importer; change image contents at the same stable ID.
Copy-Item -LiteralPath "$engineSource/tests/fixtures/images/rgb.jpg" -Destination "$project/assets/renamed.png" -Force
Send @(@{op='project.build'})|Out-Null
$changed=Get-Content -LiteralPath "$project/build/import-report.json" -Raw|ConvertFrom-Json
Check (-not ($changed|Where-Object path -eq 'assets/main.scene.json').reused -and ($changed|Where-Object path -eq 'assets/second.scene.json').reused) 'A nested HUD image invalidates only dependent scenes'
$slot='v08-'+[guid]::NewGuid().ToString('N')
$states=Send @(@{op='preview.run'},@{op='runtime.step';frames=6;x=1},@{op='runtime.save';slot=$slot},@{op='runtime.step';frames=6;x=1},@{op='runtime.load';slot=$slot},@{op='runtime.query'},@{op='runtime.scene.switch';scene='second'},@{op='runtime.query'},@{op='runtime.stats'},@{op='preview.stop'},@{op='project.package'})
Check ([Math]::Abs($states[1].entities[0].x-$states[5].entities[0].x) -lt .01) 'Binary save restores runtime state independently of authoring'
Check ([Math]::Abs($states[7].entities[0].x-600) -lt .01) 'Scene catalog switch replaces runtime world'
Check ($states[8].result.entities -eq 1 -and $states[8].result.systems.Count -eq 3) 'Runtime diagnostics report live entities and per-system timings'
Check ((Test-Path -LiteralPath "$project/dist/scenes.gmcatalog") -and (Get-ChildItem -LiteralPath "$project/dist/scenes" -Filter '*.gmscene').Count -eq 2) 'Package includes the multi-scene catalog'
$process=Start-Process -FilePath "$project/dist/gm_player.exe" -ArgumentList '--check' -WindowStyle Hidden -Wait -PassThru
Check ($process.ExitCode -eq 0) 'Packaged Player validates binary assets and native module'
$result=@{passed=$checks.Count;checks=$checks;fixture=$fixture};$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$engineBuild/v08-verification.json" -Encoding utf8
} finally { $env:GM_ENGINE_USER_DATA=$previousUserData }
