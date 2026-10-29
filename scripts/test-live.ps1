$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/paths.ps1"
$root=$engineSource
$testRoot = Join-Path $engineBuildRoot ('sessions/qa-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$project = Join-Path $testRoot 'editor.ui.json'
$session = Join-Path $testRoot 'session'
Copy-Item -LiteralPath (Join-Path $root 'resources/default_editor.ui.json') -Destination $project
$editor = Join-Path $engineBin 'gm_editor.exe'
$cli = Join-Path $engineBin 'gmctl.exe'
$process = Start-Process -FilePath $editor -ArgumentList @('--project', ('"' + $project + '"'), '--session', ('"' + $session + '"')) -WindowStyle Hidden -PassThru
$checks = [System.Collections.Generic.List[string]]::new()
function Check($condition, [string]$description) {
    if (-not $condition) { throw "FAILED: $description" }
    $checks.Add($description)
}
function Send($request) {
    $file = Join-Path $testRoot 'request.json'
    [System.IO.File]::WriteAllText($file, ($request | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
    $result = & $cli --live $session --request $file | Out-String
    return $result | ConvertFrom-Json
}
try {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path (Join-Path $session 'session.json'))) {
        if ($timer.Elapsed.TotalSeconds -gt 15 -or $process.HasExited) { throw 'Editor did not start' }
        Start-Sleep -Milliseconds 50
    }
    $initial = Send @{op='query'}
    Check $initial.ok 'Live session accepts structured commands'
    $schema = Send @{op='describe'}
    Check ($schema.result.protocol -eq 'gm.editor/1') 'Command discovery works'
    $edit = Send @{op='set';id='brand';property='text';value='命令行验证标题';expected_revision=0}
    Check ($edit.ok -and $edit.dirty -and $edit.revision -eq 1) 'Command edits update revision and dirty state'
    $conflict = Send @{op='set';id='brand';property='x';value=80;expected_revision=0}
    Check (-not $conflict.ok -and $conflict.revision -eq 1) 'Stale revision is rejected'
    $badBatch = Send @{op='batch';commands=@(@{op='set';id='brand';property='text';value='must rollback'},@{op='set';id='brand';property='w';value=-1})}
    $brand = Send @{op='query';id='brand'}
    Check (-not $badBatch.ok -and $brand.result.text -eq '命令行验证标题') 'Invalid batch has no partial effect'
    $add = Send @{op='create';type='button'}
    Check ($add.ok -and $add.selected -eq 'button_1') 'GUI and CLI share node creation'
    $undo = Send @{op='undo'}
    $gone = Send @{op='query';id='button_1'}
    Check ($undo.ok -and -not $gone.ok) 'Undo removes newly added widget'
    $redo = Send @{op='redo'}
    Check $redo.ok 'Redo restores newly added widget'
    $save = Send @{op='save'}
    $disk = Get-Content -LiteralPath $project -Raw -Encoding utf8 | ConvertFrom-Json
    $diskBrand = $disk.root.children | Where-Object id -eq 'menu' | Select-Object -ExpandProperty children | Where-Object id -eq 'brand'
    Check ($save.ok -and -not $save.dirty -and $diskBrand.text -eq '命令行验证标题') 'Live save persists UTF-8 document'
    $removed = Send @{op='remove';id='properties'}
    $apply = Send @{op='apply'}
    Check ($removed.ok -and $apply.ok) 'Editor can apply a shell without its inspector'
    $recover = Send @{op='recover'}
    $stillRemoved = Send @{op='query';id='properties'}
    Check ($recover.ok -and -not $stillRemoved.ok) 'Recovery restores tools without overwriting the document'
    $reload = Send @{op='reload';discard=$true}
    $restored = Send @{op='query';id='properties'}
    Check ($reload.ok -and $restored.ok -and -not $reload.dirty) 'Reload restores saved document'
    $report = @{passed=$checks.Count;checks=$checks;fixture=$testRoot;platform='Windows x64 / bgfx D3D11'}
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $engineBuild 'live-verification.json') -Encoding utf8
    $checks | ForEach-Object { Write-Host "PASS: $_" }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
}
