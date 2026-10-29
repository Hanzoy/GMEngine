param([string]$Compiler)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$tools=Join-Path $engineBuildRoot 'tools/InnoSetup'
if(-not $Compiler){$Compiler=Join-Path $tools 'ISCC.exe'}
if(-not (Test-Path -LiteralPath $Compiler)){
    # Pinned official immutable release, checked before execution.
    $setup=Join-Path $engineBuildRoot 'tools/innosetup-6.7.4.exe'
    New-Item -ItemType Directory -Force (Split-Path $setup -Parent)|Out-Null
    if(-not (Test-Path -LiteralPath $setup)){Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-6.7.4.exe' -OutFile $setup}
    if((Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash -ne 'A549929319F9120716677C685535CA4671E4B937B5FDC87E2585F32D8EB30E8C'){throw 'Inno Setup checksum mismatch'}
    $process=Start-Process -FilePath $setup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER',('/DIR="'+$tools+'"'),'/NOICONS') -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -ne 0){throw 'Inno Setup compiler installation failed'}
}
$stage=Join-Path $engineBuildRoot ('staging/GMEngine-'+[guid]::NewGuid().ToString('N'))
& cmake --install $engineBuild --config Release --prefix $stage
if($LASTEXITCODE -ne 0){throw 'Distribution staging failed'}
$output=Join-Path $engineBuildRoot 'releases'
New-Item -ItemType Directory -Force $output|Out-Null
& $Compiler "/DEngineDir=$stage" "/DOutputPath=$output" "$engineSource/resources/installer.iss"
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
$zip=Join-Path $output 'GMEngine-0.8.0-Windows-x64.zip'
Compress-Archive -Path "$stage/*" -DestinationPath $zip -Force
Get-FileHash -LiteralPath (Join-Path $output 'GMEngine-0.8.0-Windows-x64-Setup.exe'),$zip -Algorithm SHA256 | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'SHA256.json') -Encoding utf8
Write-Output "Installer and portable archive: $output"
