param([Parameter(Mandatory)][int]$ProcessId,[int]$Samples=3,[int]$Seconds=2,[string]$Output)
$ErrorActionPreference='Stop'
$records=@()
for($i=0;$i -lt $Samples;$i++) {
    $p=Get-Process -Id $ProcessId
    $cpu=$p.TotalProcessorTime.TotalSeconds
    $timer=[Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds $Seconds
    $p.Refresh()
    if($p.HasExited) {throw 'Measured process exited'}
    $elapsed=$timer.Elapsed.TotalSeconds
    $records += [pscustomobject]@{sample=$i;seconds=$elapsed;one_core_percent=100*($p.TotalProcessorTime.TotalSeconds-$cpu)/$elapsed;private_mib=$p.PrivateMemorySize64/1MB;working_set_mib=$p.WorkingSet64/1MB}
}
$result=[pscustomobject]@{process=$p.Path;pid=$ProcessId;samples=$records}
if($Output) {$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output -Encoding utf8}
$result | ConvertTo-Json -Depth 5
