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
$shaders=@(
    @('vs_2d','vertex','varying.def.sc'),
    @('vs_mesh','vertex','varying_3d.def.sc'),
    @('vs_mesh_instanced','vertex','varying_3d.def.sc'),
    @('fs_material','fragment','varying_3d.def.sc'),
    @('fs_shadow','fragment','varying_3d.def.sc'),
    @('fs_rsm','fragment','varying_3d.def.sc'),
    @('fs_sky','fragment','varying_post.def.sc'),
    @('vs_post','vertex','varying_post.def.sc'),
    @('fs_bloom','fragment','varying_post.def.sc'),
    @('fs_blur','fragment','varying_post.def.sc'),
    @('fs_post','fragment','varying_post.def.sc')
)
foreach($shader in $shaders){foreach($target in @(@('windows','s_5_0','dxbc'),@('android','300_es','essl'))){
    $header=Join-Path $engineSource "src/shaders/$($shader[0])_$($target[2]).bin.h"
    & $ShaderCompiler -f "$engineSource/src/shaders/$($shader[0]).sc" -o $header --type $shader[1] --platform $target[0] -p $target[1] --varyingdef "$engineSource/src/shaders/$($shader[2])" -i "$engineSource/third_party/bgfx/src" --bin2c "$($shader[0])_$($target[2])" -O 3
    if($LASTEXITCODE){throw "Shader compilation failed: $($shader[0]) / $($target[0])"}
    [IO.File]::WriteAllLines($header,@(Get-Content -LiteralPath $header|ForEach-Object {$_.TrimEnd()}),[Text.UTF8Encoding]::new($false))
}}

foreach($shader in @(@('vs_custom','vertex'),@('fs_custom','fragment'))){foreach($target in @(@('windows','s_5_0','dxbc'),@('android','300_es','essl'))){
    $fixture=Join-Path $engineSource 'tests/fixtures/render'
    $header=Join-Path $fixture "$($shader[0])_$($target[2]).bin.h"
    & $ShaderCompiler -f "$fixture/$($shader[0]).sc" -o $header --type $shader[1] --platform $target[0] -p $target[1] --varyingdef "$fixture/varying.def.sc" -i "$engineSource/third_party/bgfx/src" --bin2c "$($shader[0])_$($target[2])" -O 3
    if($LASTEXITCODE){throw 'Custom shader fixture compilation failed'}
    [IO.File]::WriteAllLines($header,@(Get-Content -LiteralPath $header|ForEach-Object {$_.TrimEnd()}),[Text.UTF8Encoding]::new($false))
}}
