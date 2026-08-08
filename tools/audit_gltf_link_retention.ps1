param(
    [string]$MapPath = "build\release_profiles\intro\bin\Release\minimal_intro_packed.map"
)

$ErrorActionPreference = "Stop"
$repository_root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($MapPath)) {
    $MapPath = Join-Path $repository_root $MapPath
}
if (-not (Test-Path -LiteralPath $MapPath -PathType Leaf)) {
    throw "Linker map not found: $MapPath"
}

$map_text = Get-Content -Raw -LiteralPath $MapPath
$checks = @(
    [pscustomobject]@{ Symbol = "LoadMeshFromMemory"; Pattern = "?LoadMeshFromMemory@gltf"; Expected = $true },
    [pscustomobject]@{ Symbol = "BuildAnimatedNodeDeltaMatricesAll"; Pattern = "?BuildAnimatedNodeDeltaMatricesAll@gltf"; Expected = $true },
    [pscustomobject]@{ Symbol = "LoadMesh (filesystem/editor)"; Pattern = "?LoadMesh@gltf"; Expected = $false },
    [pscustomobject]@{ Symbol = "ExtractTextures (editor)"; Pattern = "?ExtractTextures@gltf"; Expected = $false },
    [pscustomobject]@{ Symbol = "BuildAnimatedNodeDeltaMatrices (unused variant)"; Pattern = "?BuildAnimatedNodeDeltaMatrices@gltf"; Expected = $false }
)

$failed = $false
$results = foreach ($check in $checks) {
    $retained = $map_text.Contains($check.Pattern)
    if ($retained -ne $check.Expected) { $failed = $true }
    [pscustomobject]@{
        Symbol = $check.Symbol
        Expected = $(if ($check.Expected) { "retained" } else { "absent" })
        Actual = $(if ($retained) { "retained" } else { "absent" })
    }
}

Write-Host "glTF linker-retention audit"
Write-Host "Map: $MapPath"
$results | Format-Table -AutoSize
if ($failed) { throw "glTF linker-retention audit failed" }
Write-Host "[gltf_link_retention] PASS"

