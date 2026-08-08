param(
    [string]$Configuration = "Release",
    [string]$ReleaseName = "HiMYM_Editor_Windows_x64"
)

$ErrorActionPreference = "Stop"

$repository_root = Split-Path -Parent $PSScriptRoot
$editor_executable = Join-Path $repository_root "build\bin\$Configuration\editor_app.exe"
$release_root = Join-Path $repository_root "release"
$stage_root = Join-Path $release_root $ReleaseName
$archive_path = Join-Path $release_root "$ReleaseName.zip"

if (-not (Test-Path -LiteralPath $editor_executable -PathType Leaf)) {
    throw "Editor executable not found: $editor_executable"
}

$resolved_release_root = [System.IO.Path]::GetFullPath($release_root)
$resolved_stage_root = [System.IO.Path]::GetFullPath($stage_root)
if (-not $resolved_stage_root.StartsWith($resolved_release_root + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to package outside the repository release directory: $resolved_stage_root"
}

if (Test-Path -LiteralPath $stage_root) {
    Remove-Item -LiteralPath $stage_root -Recurse -Force
}
if (Test-Path -LiteralPath $archive_path) {
    Remove-Item -LiteralPath $archive_path -Force
}

$editor_output = Join-Path $stage_root "build\bin\Release"
$blender_output = Join-Path $stage_root "blender"
$docs_output = Join-Path $stage_root "docs"
New-Item -ItemType Directory -Force -Path $editor_output, $blender_output, $docs_output | Out-Null

Copy-Item -LiteralPath $editor_executable -Destination (Join-Path $editor_output "editor_app.exe")
Copy-Item -LiteralPath (Join-Path $repository_root "tools\editor_release\README.md") -Destination (Join-Path $stage_root "README.md")
Copy-Item -LiteralPath (Join-Path $repository_root "tools\blender\himym_blender.py") -Destination $blender_output
Copy-Item -LiteralPath (Join-Path $repository_root "tools\blender\himym_template.blend") -Destination $blender_output
Copy-Item -LiteralPath (Join-Path $repository_root "PR\guides\BLENDER_GLTF_GUIDE.md") -Destination $blender_output
Copy-Item -LiteralPath (Join-Path $repository_root "PR\guides\EDITOR_GUIDE.md") -Destination $docs_output
Copy-Item -LiteralPath (Join-Path $repository_root "PACKED_RUNTIME_SIZE.md") -Destination $docs_output
Copy-Item -LiteralPath (Join-Path $repository_root "ROADMAP.md") -Destination $docs_output

$imgui_ini = Join-Path $repository_root "build\bin\$Configuration\imgui.ini"
if (Test-Path -LiteralPath $imgui_ini -PathType Leaf) {
    Copy-Item -LiteralPath $imgui_ini -Destination (Join-Path $editor_output "imgui.ini")
}

$addon_stage = Join-Path $blender_output "himym_blender"
New-Item -ItemType Directory -Force -Path $addon_stage | Out-Null
Copy-Item -LiteralPath (Join-Path $repository_root "tools\blender\himym_blender.py") `
    -Destination (Join-Path $addon_stage "__init__.py")
Compress-Archive `
    -LiteralPath $addon_stage `
    -DestinationPath (Join-Path $blender_output "himym_blender_addon.zip") `
    -CompressionLevel Optimal
Remove-Item -LiteralPath $addon_stage -Recurse -Force

$version_lines = @(
    "HiMYM Editor Windows x64"
    "Configuration: $Configuration"
    "Packaged: $([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))"
    "Editor SHA256: $((Get-FileHash -Algorithm SHA256 -LiteralPath $editor_executable).Hash)"
)
Set-Content -LiteralPath (Join-Path $stage_root "RELEASE_INFO.txt") -Value $version_lines -Encoding UTF8

Compress-Archive -Path (Join-Path $stage_root "*") -DestinationPath $archive_path -CompressionLevel Optimal

$archive = Get-Item -LiteralPath $archive_path
$editor = Get-Item -LiteralPath $editor_executable
Write-Host "Editor release created"
Write-Host "  Folder:  $stage_root"
Write-Host "  Archive: $archive_path"
Write-Host "  Editor:  $($editor.Length) bytes"
Write-Host "  ZIP:     $($archive.Length) bytes"
