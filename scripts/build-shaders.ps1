param([string]$ShaderCompiler)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/paths.ps1"
if(-not $ShaderCompiler){
    $build=Join-Path $engineBuildRoot 'shader-tools'
    & cmake -S "$engineSource/third_party/bgfx.cmake" -B $build -G 'Visual Studio 17 2022' -A x64 "-DBGFX_DIR=$engineSource/third_party/bgfx" "-DBX_DIR=$engineSource/third_party/bx" "-DBIMG_DIR=$engineSource/third_party/bimg" -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_BUILD_TOOLS=ON -DBGFX_BUILD_TOOLS_SHADER=ON -DBGFX_BUILD_TOOLS_GEOMETRY=OFF -DBGFX_BUILD_TOOLS_TEXTURE=OFF -DBGFX_INSTALL=OFF
    if($LASTEXITCODE){throw 'Shader compiler configuration failed'}
    & cmake --build $build --config Release --target shaderc --parallel 4
    if($LASTEXITCODE){throw 'Shader compiler build failed'}
    $ShaderCompiler=Join-Path $build 'cmake/bgfx/Release/shaderc.exe'
}
foreach($target in @(@('windows','s_5_0','dxbc'),@('android','300_es','essl'))){
    & $ShaderCompiler -f "$engineSource/src/shaders/vs_2d.sc" -o "$engineSource/src/shaders/vs_2d_$($target[2]).bin.h" --type vertex --platform $target[0] -p $target[1] --varyingdef "$engineSource/src/shaders/varying.def.sc" -i "$engineSource/third_party/bgfx/src" --bin2c "vs_2d_$($target[2])" -O 3
    if($LASTEXITCODE){throw "2D shader compilation failed: $($target[0])"}
    $header=Join-Path $engineSource "src/shaders/vs_2d_$($target[2]).bin.h"
    # shaderc's ASCII preview comments can end in a space; normalize source only.
    [IO.File]::WriteAllLines($header,@(Get-Content -LiteralPath $header|ForEach-Object {$_.TrimEnd()}),[Text.UTF8Encoding]::new($false))
}
