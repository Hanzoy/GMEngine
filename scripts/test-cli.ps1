$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/paths.ps1"
$root=$engineSource
$fixture = Join-Path $engineBuildRoot ('sessions/cli-qa-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$project = Join-Path $fixture 'main.ui.json'
Copy-Item -LiteralPath (Join-Path $root 'tests/fixtures/ui/empty.ui.json') -Destination $project
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = Join-Path $engineBin 'gmctl.exe'
$start.Arguments = '--project "' + $project + '" --stdio'
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.StandardInputEncoding = [System.Text.UTF8Encoding]::new($false)
$start.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
$process = [System.Diagnostics.Process]::Start($start)
$requests = @(
    @{op='describe'},
    @{op='create';type='label'},
    @{op='set';id='label_1';property='text';value='命令行创建'},
    @{op='undo'},
    @{op='redo'},
    @{op='save'},
    @{op='query';id='label_1'}
)
foreach ($request in $requests) { $process.StandardInput.WriteLine(($request | ConvertTo-Json -Compress)) }
$process.StandardInput.Close()
$responses = @($process.StandardOutput.ReadToEnd().Trim() -split '\r?\n' | ForEach-Object { $_ | ConvertFrom-Json })
$process.WaitForExit()
if ($process.ExitCode -ne 0 -or $responses.Count -ne $requests.Count) { throw 'CLI session failed' }
foreach ($response in $responses) { if (-not $response.ok) { throw ($response | ConvertTo-Json) } }
if ($responses[-1].result.text -ne '命令行创建' -or $responses[-1].dirty) { throw 'CLI undo/redo/save roundtrip failed' }
$disk = Get-Content -LiteralPath $project -Raw -Encoding utf8 | ConvertFrom-Json
if (($disk.root.children | Where-Object id -eq 'label_1').text -ne '命令行创建') { throw 'UTF-8 persistence failed' }
@{passed=$responses.Count;guiRequired=$false;fixture=$fixture} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $engineBuild 'cli-verification.json') -Encoding utf8
Write-Host 'PASS: standalone CLI discovery, create, UTF-8 edit, undo, redo, save and query (7 commands)'
