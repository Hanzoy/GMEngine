param([switch]$Apply)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$gameRoot=[IO.Path]::GetFullPath((Join-Path $engineSource '../GMEngineGame'))
$reportRoot=Join-Path $engineBuildRoot ('reports/cleanup-'+(Get-Date -Format 'yyyy-MM-dd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$paths=[Collections.Generic.List[string]]::new()
# Explicit obsolete output directories only. Never prune source, dependencies, saves or current build.
foreach($name in @('staging','sessions','ui-review','ci-msvc-compat','headless-v010','verify-081','verify-082','archive/source-tree-build','archive/source-tree-sessions')){
    $paths.Add((Join-Path $engineBuildRoot $name))
}
Get-ChildItem -LiteralPath $engineBuildRoot -Directory -Filter 'sdk-portability-*' | ForEach-Object {$paths.Add($_.FullName)}
if(Test-Path -LiteralPath (Join-Path $engineBuildRoot 'navigation-qa')){Get-ChildItem -LiteralPath (Join-Path $engineBuildRoot 'navigation-qa') -Directory | ForEach-Object {$paths.Add($_.FullName)}}
foreach($name in @('archives/builds/CrystalMeadow-v07','archives/legacy/CrystalMeadow-build-before-move','.cache/engine-contracts-v08')){$paths.Add((Join-Path $gameRoot $name))}
if(Test-Path -LiteralPath (Join-Path $gameRoot '.cache')){Get-ChildItem -LiteralPath (Join-Path $gameRoot '.cache') -Directory -Filter 'scene-qa-*' | ForEach-Object {$paths.Add($_.FullName)}}
$currentVersion=[version](Get-Content -LiteralPath (Join-Path $engineSdk 'engine-sdk.json') -Raw | ConvertFrom-Json).version
if(Test-Path -LiteralPath (Join-Path $engineBuildRoot 'releases')){Get-ChildItem -LiteralPath (Join-Path $engineBuildRoot 'releases') -File | Where-Object {$_.Name -match '^GMEngine-(\d+\.\d+\.\d+)-(Windows-x64(-Setup)?\.(exe|zip)|SHA256\.json)$' -and [version]$Matches[1] -lt $currentVersion} | ForEach-Object {$paths.Add($_.FullName)}}
foreach($name in @('fix-dsp-two.cjs','fix-dsp.cjs','patch-dsp-engine.cjs','patch-dsp-remaining.cjs','navigation-preview.ps1','ui-review-paused.json')){$paths.Add((Join-Path $engineBuildRoot $name))}
$oldEditor=Join-Path $engineSdk 'bin/gm_editor.exe.previous-0.10.1'
if(Test-Path -LiteralPath $oldEditor){$paths.Add($oldEditor)}
$running=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(gm_|cmake|cl\.exe|MSBuild)'} )
$report=@()
foreach($candidate in $paths){
    $absolute=[IO.Path]::GetFullPath($candidate)
    $allowed=$false
    foreach($root in @($engineBuildRoot,$gameRoot,$engineSdk)) {if($absolute.StartsWith($root.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)){$allowed=$true}}
    if(-not $allowed){throw "Cleanup path escapes output roots: $absolute"}
    if(-not (Test-Path -LiteralPath $absolute)){continue}
    $item=Get-Item -LiteralPath $absolute
    $ancestor=$item
    while($ancestor){
        if($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Refusing reparse ancestor: $($ancestor.FullName)"}
        $ancestor=if($ancestor.PSIsContainer){$ancestor.Parent}else{$ancestor.Directory}
    }
    $nested=@(if($item.PSIsContainer){Get-ChildItem -LiteralPath $absolute -Force -Recurse})
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or @($nested | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw "Refusing reparse point: $absolute"}
    foreach($process in $running){if($process.ExecutablePath -and ($process.ExecutablePath.StartsWith($absolute,[StringComparison]::OrdinalIgnoreCase) -or ($process.CommandLine -and $process.CommandLine.Contains($absolute)))){throw "Output still in use: $absolute"}}
    $files=@(if($item.PSIsContainer){$nested | Where-Object {-not $_.PSIsContainer}}else{$item})
    $bytes=($files | Measure-Object Length -Sum).Sum
    $report+=@{path=$absolute;files=$files.Count;bytes=[long]$bytes;action=if($Apply){'removed'}else{'planned'}}
    if($Apply){
        # Retain small validation evidence before removing generated fixtures.
        $evidence=Join-Path $reportRoot ('evidence/'+($absolute -replace '[:\\/]','_'))
        foreach($file in $files | Where-Object {$_.Length -lt 4MB -and ($_.Name -match '(verification|validation|benchmark|ctest|acceptance).*\.(json|log|csv)$')}){
            New-Item -ItemType Directory -Force $evidence | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $evidence $file.Name)
        }
        # The fully resolved absolute target was checked above; all deletion stays in PowerShell.
        Remove-Item -LiteralPath $absolute -Recurse -Force
    }
}
New-Item -ItemType Directory -Force $reportRoot | Out-Null
@{date=(Get-Date -Format o);applied=[bool]$Apply;files=($report | Measure-Object files -Sum).Sum;bytes=($report | Measure-Object bytes -Sum).Sum;items=$report} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $reportRoot 'manifest.json') -Encoding utf8
if($Apply){
    $logs=Join-Path $reportRoot 'logs'
    New-Item -ItemType Directory -Force $logs | Out-Null
    Get-ChildItem -LiteralPath $engineBuildRoot -File -Filter '*.log' | ForEach-Object {Move-Item -LiteralPath $_.FullName -Destination (Join-Path $logs $_.Name)}
}
"Cleanup: $($report.Count) targets; $([math]::Round((($report | Measure-Object bytes -Sum).Sum)/1MB,2)) MiB; apply=$Apply"
"Manifest: $reportRoot/manifest.json"
