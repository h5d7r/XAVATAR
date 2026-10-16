param(
    [string]$Distribution = 'Ubuntu',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'Install WSL and an Ubuntu distribution before building XAVATAR.'
}
$windowsPath = $PSScriptRoot.Replace('\', '/')
$projectPath = (& wsl.exe -d $Distribution --exec wslpath -a $windowsPath | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $projectPath) {
    throw "Could not open the project in the WSL distribution '$Distribution'."
}
$buildScript = "$projectPath/tools/build.sh"
if ($Clean) {
    & wsl.exe -d $Distribution --exec bash $buildScript --clean
} else {
    & wsl.exe -d $Distribution --exec bash $buildScript
}
if ($LASTEXITCODE -ne 0) {
    throw "XAVATAR build failed (exit code $LASTEXITCODE)."
}
Write-Host 'Ready: build/XAVATAR.elf and build/XAVATAR-installer.elf'
