param([string]$Project,[string]$Session)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$executable=Join-Path $engineBin 'gm_editor.exe'
if(-not (Test-Path -LiteralPath $executable)){& "$PSScriptRoot/build.ps1"}
$editorArguments=@()
if($Project){$editorArguments+=@('--project',$Project)}
if($Session){$editorArguments+=@('--session',$Session)}
& $executable @editorArguments
