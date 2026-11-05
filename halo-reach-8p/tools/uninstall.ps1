# Reach8 - desinstalador
# Quita el WTSAPI32.dll de Reach8 de mcc\binaries\win64 y restaura la copia de
# seguridad más reciente (si el instalador la creó). Tu configuración en
# Documentos\Reach8 se conserva.
#
# Uso:  powershell -ExecutionPolicy Bypass -File uninstall.ps1 [-MccPath "RUTA"] [-NoRestore]

param([string]$MccPath = "", [switch]$NoRestore)

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
    Write-Host "No encuentro Halo MCC. Ejecuta: uninstall.ps1 -MccPath ""RUTA""" -ForegroundColor Red
    exit 1
}

$win64 = Join-Path $MccPath "mcc\binaries\win64"
$target = Join-Path $win64 "WTSAPI32.dll"

if (-not (Test-Path $target)) {
    Write-Host "No hay WTSAPI32.dll en $win64. Nada que quitar." -ForegroundColor Yellow
} else {
    $desc = (Get-Item $target).VersionInfo.FileDescription
    if ($desc -notlike "*Reach8*") {
        Write-Host "El WTSAPI32.dll instalado NO es de Reach8 ($desc). No se toca." -ForegroundColor Yellow
        exit 0
    }
    Remove-Item $target -Force
    Write-Host "Reach8 eliminado de $win64" -ForegroundColor Green
}

$backupDir = Join-Path $win64 "Reach8_backup"
if (-not $NoRestore -and (Test-Path $backupDir)) {
    $latest = Get-ChildItem $backupDir -Filter "WTSAPI32.dll.*" | Sort-Object Name -Descending | Select-Object -First 1
    if ($latest) {
        Copy-Item $latest.FullName $target -Force
        Write-Host "Restaurado el WTSAPI32.dll anterior: $($latest.Name)" -ForegroundColor Green
    }
}

Write-Host "Listo. Ya puedes iniciar MCC con antitrampas."
