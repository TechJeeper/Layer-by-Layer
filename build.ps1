<#
.SYNOPSIS
  Builds Layer-by-Layer on Windows using the Pebble SDK in a container.

.EXAMPLE
  ./build.ps1                       # build build/LayerByLayer.pbw
  ./build.ps1 -Phone 192.168.1.23   # build, then install via developer connection
  ./build.ps1 -Shell                # open a shell with the `pebble` tool
#>
param(
    [string]$Phone,
    [switch]$Shell
)
$engine = if (Get-Command podman -ErrorAction SilentlyContinue) { 'podman' }
          elseif (Get-Command docker -ErrorAction SilentlyContinue) { 'docker' }
          else { throw 'Install Podman Desktop or Docker Desktop first.' }

$root = $PSScriptRoot
$image = 'pebble-sdk'
$volume = 'pebble-sdk'

& $engine image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Building the $image image (first run only)..."
    & $engine build -t $image (Join-Path $root 'build-env')
    if ($LASTEXITCODE -ne 0) { throw 'Image build failed.' }
}

# Mount path basename becomes the .pbw name (build/LayerByLayer.pbw).
$run = @('run', '--rm', '-v', "${root}:/LayerByLayer", '-w', '/LayerByLayer', '-v', "${volume}:/root/.pebble-sdk")
$setup = 'pebble sdk list 2>/dev/null | grep -q active || pebble sdk install latest'

if ($Shell) {
    & $engine @run -it $image bash -c "$setup; exec bash"
    exit $LASTEXITCODE
}

$cmd = "$setup && pebble build"
if ($Phone) { $cmd += " && pebble install --phone $Phone" }
& $engine @run $image bash -c $cmd
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Host "Built $(Join-Path $root 'build\LayerByLayer.pbw')"
