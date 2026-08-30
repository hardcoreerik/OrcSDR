$ErrorActionPreference = 'Stop'
$appRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$patch = Join-Path $PSScriptRoot 'patches\m5gfx-tab5-pageflip.patch'

& git -C $appRoot apply --check -- $patch 2>$null
if ($LASTEXITCODE -eq 0) {
  & git -C $appRoot apply --whitespace=nowarn -- $patch
  if ($LASTEXITCODE -ne 0) { throw 'Unable to apply the M5GFX Tab5 page-flip patch.' }
  exit 0
}

& git -C $appRoot apply --reverse --check -- $patch 2>$null
if ($LASTEXITCODE -ne 0) {
  throw 'The installed M5GFX component does not match the Tab5 page-flip patch.'
}
