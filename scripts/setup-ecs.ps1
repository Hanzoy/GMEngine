$ErrorActionPreference = 'Stop'
$ecsRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'third_party'
$ecsFiles = @(
    @{ Path = 'flecs/flecs.h'; Url = 'https://raw.githubusercontent.com/SanderMertens/flecs/v4.1.6/distr/flecs.h' },
    @{ Path = 'flecs/flecs.c'; Url = 'https://raw.githubusercontent.com/SanderMertens/flecs/v4.1.6/distr/flecs.c' },
    @{ Path = 'flecs/LICENSE'; Url = 'https://raw.githubusercontent.com/SanderMertens/flecs/v4.1.6/LICENSE' },
    @{ Path = 'entt/entt.hpp'; Url = 'https://raw.githubusercontent.com/skypjack/entt/v4.0.0/single_include/entt/entt.hpp' },
    @{ Path = 'entt/LICENSE'; Url = 'https://raw.githubusercontent.com/skypjack/entt/v4.0.0/LICENSE' },
    @{ Path = 'entt-3.16/entt.hpp'; Url = 'https://raw.githubusercontent.com/skypjack/entt/v3.16.0/single_include/entt/entt.hpp' },
    @{ Path = 'entt-3.16/LICENSE'; Url = 'https://raw.githubusercontent.com/skypjack/entt/v3.16.0/LICENSE' }
)
foreach ($ecsFile in $ecsFiles) {
    $ecsTarget = Join-Path $ecsRoot $ecsFile.Path
    New-Item -ItemType Directory -Force -Path (Split-Path $ecsTarget -Parent) | Out-Null
    if (-not (Test-Path -LiteralPath $ecsTarget)) {
        Write-Host "Downloading $($ecsFile.Path)"
        Invoke-WebRequest -Uri $ecsFile.Url -OutFile $ecsTarget
    }
}
$ecsHashes = @{
    'flecs/flecs.c'='6005392EB13C0F3C7ABDECB2F85271E50934F63919AFEBF0BBE85F6DFC7320D6'
    'flecs/flecs.h'='526036A5A41678E2A43A3CB835E9EAA70FD1993868B1978950C0D275752F69B1'
    'entt/entt.hpp'='EEDCB83FEDFFE640334EF69A0647625A186CF1DAEB333B21B0A1445F01E89B6C'
    'entt-3.16/entt.hpp'='3272191CF3D46CEC92368F918695502DC8D174C92E0B8B4A5D53FED28E240C9F'
}
foreach ($ecsRelative in $ecsHashes.Keys) {
    if ((Get-FileHash -LiteralPath (Join-Path $ecsRoot $ecsRelative) -Algorithm SHA256).Hash -ne $ecsHashes[$ecsRelative]) { throw "Dependency checksum mismatch: $ecsRelative" }
}
Write-Host 'Flecs 4.1.6, EnTT 4.0.0 and MSVC-compatible EnTT 3.16.0 are ready.'
