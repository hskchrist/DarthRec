# Reach8 - instalador
# Copia WTSAPI32.dll (Reach8) a mcc\binaries\win64. Si ya existe otro WTSAPI32.dll
# (por ejemplo AlphaRing), lo guarda en Reach8_backup antes de reemplazarlo.
# No modifica ningún archivo del juego.
#
# Uso:  clic derecho > "Ejecutar con PowerShell"
#   o:  powershell -ExecutionPolicy Bypass -File install.ps1 [-MccPath "RUTA"]

param([string]$MccPath = "")

$ErrorActionPreference = "Stop"

function Find-Mcc {
    $candidates = @()
    try {
        $steam = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -Name SteamPath).SteamPath
        if ($steam) {
            $candidates += (Join-Path $steam "steamapps\common\Halo The Master Chief Collection")
            $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
            if (Test-Path $vdf) {
                foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                    $lib = $m.Groups[1].Value -replace '\\\\', '\'
                    $candidates += (Join-Path $lib "steamapps\common\Halo The Master Chief Collection")
                }
            }
        }
    } catch { }
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "mcc\binaries\win64\MCC-Win64-Shipping.exe")) { return $c }
    }
    return $null
}

if (-not $MccPath) { $MccPath = Find-Mcc }
if (-not $MccPath) {
    Write-Host "No encuentro Halo MCC. Ejecuta: install.ps1 -MccPath ""RUTA de Halo The Master Chief Collection""" -ForegroundColor Red
    exit 1
}

$win64 = Join-Path $MccPath "mcc\binaries\win64"
if (-not (Test-Path (Join-Path $win64 "MCC-Win64-Shipping.exe"))) {
    Write-Host "La carpeta no parece de MCC (falta MCC-Win64-Shipping.exe): $win64" -ForegroundColor Red
    exit 1
}

$source = Join-Path $PSScriptRoot "WTSAPI32.dll"
if (-not (Test-Path $source)) {
    Write-Host "Falta WTSAPI32.dll junto a este script." -ForegroundColor Red
    exit 1
}

$target = Join-Path $win64 "WTSAPI32.dll"
if (Test-Path $target) {
    $same = (Get-FileHash $target).Hash -eq (Get-FileHash $source).Hash
    if (-not $same) {
        $backupDir = Join-Path $win64 "Reach8_backup"
        New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
        $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
        $backup = Join-Path $backupDir "WTSAPI32.dll.$stamp"
        Copy-Item $target $backup
        Write-Host "Había otro WTSAPI32.dll (¿otro mod?). Copia de seguridad: $backup" -ForegroundColor Yellow
    }
}

Copy-Item $source $target -Force
Write-Host "Reach8 instalado en: $target" -ForegroundColor Green

$docs = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Reach8"
New-Item -ItemType Directory -Force -Path $docs | Out-Null
$ini = Join-Path $docs "reach8.ini"
$iniSource = Join-Path $PSScriptRoot "reach8.ini"
if (-not (Test-Path $ini) -and (Test-Path $iniSource)) {
    Copy-Item $iniSource $ini
    Write-Host "Configuración inicial: $ini"
}

Write-Host ""
Write-Host "Siguiente paso:" -ForegroundColor Cyan
Write-Host " 1. En Steam pulsa Jugar en Halo MCC y elige 'Anti-Cheat Disabled (Mods and Limited Services)'."
Write-Host " 2. Dentro del juego pulsa F8 (o Back+Start en el mando del jugador 1) para abrir el menú de Reach8."
Write-Host " 3. Antes de jugar CON antitrampas (matchmaking), ejecuta uninstall.ps1."
