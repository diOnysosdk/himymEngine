param(
  [string[]]$ProjectCues = @(),
  [string[]]$SearchRoots = @(),
  [switch]$IncludeWorkspaceCues = $true
)

Set-Location "e:\code\cpp\mono\himym"
$workspaceRoot = (Get-Location).Path

function Add-ProjectIfValid([System.Collections.Generic.List[object]]$target, [string]$cuesPath, [string]$rootHint) {
  if ([string]::IsNullOrWhiteSpace($cuesPath)) { return }
  $fullCues = [System.IO.Path]::GetFullPath($cuesPath)
  if (!(Test-Path $fullCues)) { return }
  if ((Get-Item $fullCues).PSIsContainer) { return }
  if ([System.IO.Path]::GetFileName($fullCues).ToLowerInvariant() -ne 'cues.txt') { return }

  $projectDir = Split-Path $fullCues -Parent
  $name = Split-Path $projectDir -Leaf
  $root = $rootHint

  if ([string]::IsNullOrWhiteSpace($root)) {
    if ($projectDir.StartsWith($workspaceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
      $root = $workspaceRoot
    } else {
      $root = $projectDir
    }
  }

  $target.Add([pscustomobject]@{
    Name = $name
    Cues = $fullCues
    Root = $root
  })
}

function Get-Projects {
  $found = New-Object 'System.Collections.Generic.List[object]'

  foreach ($pc in $ProjectCues) {
    Add-ProjectIfValid $found $pc ""
  }

  if ($IncludeWorkspaceCues) {
    $workspaceCues = Get-ChildItem -Path $workspaceRoot -Recurse -File -Filter cues.txt |
      Where-Object {
        $_.FullName -notlike '*\build\*' -and
        $_.FullName -notlike '*\build_clang\*' -and
        $_.FullName -notlike '*\build_mingw\*'
      }
    foreach ($f in $workspaceCues) {
      Add-ProjectIfValid $found $f.FullName $workspaceRoot
    }
  }

  foreach ($sr in $SearchRoots) {
    if ([string]::IsNullOrWhiteSpace($sr)) { continue }
    if (!(Test-Path $sr)) { continue }
    $rootPath = [System.IO.Path]::GetFullPath($sr)
    $cuesFiles = Get-ChildItem -Path $rootPath -Recurse -File -Filter cues.txt
    foreach ($f in $cuesFiles) {
      Add-ProjectIfValid $found $f.FullName $rootPath
    }
  }

  $dedup = @{}
  foreach ($p in $found) {
    $key = $p.Cues.ToLowerInvariant()
    if (-not $dedup.ContainsKey($key)) {
      $dedup[$key] = $p
    }
  }
  return $dedup.Values | Sort-Object Name
}

$projects = Get-Projects

function Collect-AssetPaths($cuesPath){
  $lines = Get-Content $cuesPath
  $section=''
  $items=@()
  foreach($line in $lines){
    $t=$line.Trim()
    if($t -match '^\[(.+)\]$'){ $section=$matches[1]; continue }
    if($t.Length -eq 0 -or $t.StartsWith('#')){ continue }
    $parts = $t.Split('|')
    switch($section){
      'image_cues' { if($parts.Length -ge 2){ $items += [pscustomobject]@{Section=$section;Path=$parts[1]} } }
      'animated_sprite_cues' { if($parts.Length -ge 3){ foreach($fp in $parts[2].Split(';')){ if($fp){ $items += [pscustomobject]@{Section=$section;Path=$fp} } } } }
      'music_cues' { if($parts.Length -ge 2){ $items += [pscustomobject]@{Section=$section;Path=$parts[1]} } }
      'mesh_cues' { if($parts.Length -ge 3 -and $parts[2] -eq '4'){ $items += [pscustomobject]@{Section=$section;Path=$parts[1]} } }
      'text_cues' { if($parts.Length -ge 26 -and $parts[25]){ $items += [pscustomobject]@{Section=$section;Path=$parts[25]} } }
      'scroll_text_cues' { if($parts.Length -ge 48 -and $parts[47]){ $items += [pscustomobject]@{Section=$section;Path=$parts[47]} } }
    }
  }
  return $items
}

function Test-NonPackedResolve($cuesPath,$items){
  $cuesDir = Split-Path $cuesPath -Parent
  $leaf = Split-Path $cuesDir -Leaf
  $leafAssets = "${leaf}_assets"
  $missing=@()
  foreach($it in $items){
    $raw = $it.Path
    $p = $raw.Replace('/','\\')
    $ok=$false
    if([IO.Path]::IsPathRooted($p)){
      if(Test-Path $p){ $ok=$true }
    } else {
      if(Test-Path (Join-Path (Get-Location) $p)){ $ok=$true }
      if(-not $ok){ if(Test-Path (Join-Path $cuesDir $p)){ $ok=$true } }
      if(-not $ok -and ($p -notmatch '[\\/]')){
        if(Test-Path (Join-Path (Get-Location) (Join-Path 'project_assets' $p))){ $ok=$true }
      }
      if(-not $ok -and ($p -notmatch '[\\/]')){
        if(Test-Path (Join-Path $cuesDir (Join-Path 'project_assets' $p))){ $ok=$true }
      }
      if(-not $ok -and ($p -notmatch '[\\/]')){
        if(Test-Path (Join-Path (Get-Location) (Join-Path $leafAssets $p))){ $ok=$true }
      }
      if(-not $ok -and ($p -notmatch '[\\/]')){
        if(Test-Path (Join-Path $cuesDir (Join-Path $leafAssets $p))){ $ok=$true }
      }
    }
    if(-not $ok){ $missing += "$($it.Section):$raw" }
  }
  return $missing
}

$packCli='e:\code\cpp\mono\himym\build\bin\Release\pack_cli.exe'
$packedHeader='e:\code\cpp\mono\himym\build\packed_assets.h'
$rows=@()
$details=@()

foreach($p in $projects){
  if(!(Test-Path $p.Cues)){ continue }
  $items = Collect-AssetPaths $p.Cues
  $nonPackedMissing = Test-NonPackedResolve $p.Cues $items

  $cachePath = Join-Path (Split-Path $p.Cues -Parent) 'pack_cache.txt'
  $packOut = & $packCli $p.Cues $packedHeader $cachePath $p.Root 2>&1
  $packExit = $LASTEXITCODE

  $rows += [pscustomobject]@{
    Project = $p.Name
    AssetRefs = $items.Count
    NonPackedMissing = $nonPackedMissing.Count
    PackedPackResult = $(if($packExit -eq 0){'PASS'}else{'FAIL'})
  }

  if($nonPackedMissing.Count -gt 0){
    $details += "[$($p.Name)] Non-packed missing:"
    $details += ($nonPackedMissing | Select-Object -First 8)
  }
  if($packExit -ne 0){
    $details += "[$($p.Name)] Pack failed:"
    $details += ($packOut | Select-Object -First 8)
  }
}

$rows | Format-Table -AutoSize
"--- Details ---"
if($details.Count -eq 0){
  'No missing path references and no pack failures in sweep set.'
} else {
  $details
}
