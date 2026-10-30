param(
    [Parameter(Mandatory=$true)][string]$Project,
    [string]$NdkRoot,[string]$BuildTools,[string]$AndroidJar,[string]$Ninja,
    [string]$AppPackage='com.gmengine.touchlab'
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
$manifest=[IO.Path]::GetFullPath($Project)
$gameRoot=Split-Path $manifest -Parent
$toolRoot=Join-Path $engineBuildRoot 'tools/Android'
if(-not $NdkRoot){$NdkRoot=Join-Path $toolRoot 'android-ndk-r29'}
if(-not $BuildTools){$BuildTools=Join-Path $toolRoot 'android-15'}
if(-not $AndroidJar){$AndroidJar=Join-Path $toolRoot 'android-35/android.jar'}
if(-not $Ninja){$Ninja=Join-Path $engineBuildRoot 'tools/ninja/ninja.exe'}
foreach($file in @("$NdkRoot/build/cmake/android.toolchain.cmake","$BuildTools/aapt2.exe","$BuildTools/zipalign.exe","$BuildTools/apksigner.bat",$AndroidJar,$Ninja)){
    if(-not(Test-Path -LiteralPath $file)){throw "Missing Android tool: $file. Supply the tool paths from your Android SDK/NDK installation."}
}
if($AppPackage -notmatch '^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$'){throw 'Invalid Android package ID'}
$sources=@(Get-ChildItem -LiteralPath (Join-Path $gameRoot 'Scripts') -Filter '*.cpp' -Recurse | ForEach-Object {$_.FullName.Replace('\','/')})
if(-not $sources.Count){throw 'No game C++ sources found'}
$built='{"op":"project.build"}' | & "$engineBin/gmctl.exe" --project $manifest | Out-String | ConvertFrom-Json
if(-not $built.ok){throw ("Compile the Windows module and source assets first: "+$built.error)}
$native=Join-Path $engineBuildRoot ('android/'+$AppPackage)
& cmake -S $engineSource -B $native -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_TOOLCHAIN_FILE=$NdkRoot/build/cmake/android.toolchain.cmake" '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-26' '-DANDROID_STL=c++_static' '-DCMAKE_BUILD_TYPE=Release' ("-DGM_ANDROID_GAME_SOURCES="+($sources -join ';'))
if($LASTEXITCODE){throw 'Android configure failed'}
& cmake --build $native --target gm_android --parallel 4
if($LASTEXITCODE){throw 'Android native build failed'}
$stage=Join-Path $engineBuildRoot ('android-stage/'+[guid]::NewGuid().ToString('N'))
$assets=Join-Path $stage 'assets'
New-Item -ItemType Directory -Force $assets | Out-Null
Copy-Item -LiteralPath "$gameRoot/build/game.gmscene" -Destination "$assets/game.gmscene"
$names=[Collections.Generic.List[string]]::new()
foreach($name in @('scenes.gmcatalog','prefabs.gmcatalog')){$names.Add($name)}
$report=Get-Content -LiteralPath "$gameRoot/build/import-report.json" -Raw | ConvertFrom-Json
foreach($item in $report){if($item.file){$names.Add($item.file)}}
$index=Get-Content -LiteralPath "$gameRoot/.gm/assets.json" -Raw | ConvertFrom-Json
foreach($item in $index.assets){if([IO.Path]::GetExtension($item.path) -in @('.wav','.mp3','.flac')){$names.Add($item.path)}}
foreach($name in $names){
    $source=Join-Path "$gameRoot/build" $name;$target=Join-Path $assets $name
    New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
}
[IO.File]::WriteAllLines((Join-Path $assets 'content.files'),$names,[Text.UTF8Encoding]::new($false))
$fingerprint=@('game.gmscene')+@($names | Sort-Object -Unique) | ForEach-Object {$_+':'+(Get-FileHash -LiteralPath (Join-Path $assets $_) -Algorithm SHA256).Hash}
$sha=[Security.Cryptography.SHA256]::Create()
try{$version=[Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($fingerprint -join "`n"))))}finally{$sha.Dispose()}
[IO.File]::WriteAllText((Join-Path $assets 'content.version'),$version,[Text.UTF8Encoding]::new($false))
if(Test-Path -LiteralPath "$gameRoot/assets/font.ttf"){Copy-Item -LiteralPath "$gameRoot/assets/font.ttf" -Destination "$assets/font.ttf"}
Copy-Item -LiteralPath "$engineSdk/licenses" -Destination "$assets/licenses" -Recurse
Copy-Item -LiteralPath "$engineSource/resources/roboto-license.txt" -Destination "$assets/licenses/roboto.txt"
Copy-Item -LiteralPath "$NdkRoot/NOTICE" -Destination "$assets/licenses/android-ndk.txt"
Copy-Item -LiteralPath "$NdkRoot/NOTICE.toolchain" -Destination "$assets/licenses/android-toolchain.txt"
Copy-Item -LiteralPath "$engineSource/THIRD_PARTY.md" -Destination "$assets/THIRD_PARTY.md"
$base=Join-Path $stage 'base.apk'
& "$BuildTools/aapt2.exe" link -I $AndroidJar --manifest "$engineSource/platform/android/AndroidManifest.xml" -A $assets --rename-manifest-package $AppPackage -o $base
if($LASTEXITCODE){throw 'APK resource packaging failed'}
$zipCode='import sys,zipfile; z=zipfile.ZipFile(sys.argv[1],"a"); z.write(sys.argv[2],"lib/arm64-v8a/libgm_android.so",compress_type=zipfile.ZIP_STORED); z.close()'
& py -3 -c $zipCode $base "$native/libgm_android.so"
if($LASTEXITCODE){throw 'APK native library packaging failed'}
$aligned=Join-Path $stage 'aligned.apk'
& "$BuildTools/zipalign.exe" -P 16 -f 4 $base $aligned
if($LASTEXITCODE){throw 'APK alignment failed'}
$key=Join-Path $engineBuildRoot 'tools/android-debug.keystore'
if(-not(Test-Path -LiteralPath $key)){
    & keytool -genkeypair -keystore $key -alias androiddebugkey -storepass android -keypass android -dname 'CN=Android Debug,O=GMEngine,C=US' -keyalg RSA -keysize 2048 -validity 10000
    if($LASTEXITCODE){throw 'Debug signing key generation failed'}
}
$output=Join-Path $gameRoot 'dist/android'
New-Item -ItemType Directory -Force $output | Out-Null
$apk=Join-Path $output ($AppPackage+'-debug.apk')
& "$BuildTools/apksigner.bat" sign --ks $key --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out $apk $aligned
if($LASTEXITCODE){throw 'Debug APK signing failed'}
& "$BuildTools/apksigner.bat" verify --verbose $apk
if($LASTEXITCODE){throw 'APK signature verification failed'}
& "$BuildTools/zipalign.exe" -c -P 16 4 $apk
if($LASTEXITCODE){throw 'APK alignment verification failed'}
Get-FileHash -LiteralPath $apk -Algorithm SHA256
Write-Output "Built and verified debug APK: $apk"
