@echo off
:: ==========================================================
:: MouFiltr Installer (no auto-elevate, 4 steps, click-friendly)
:: ==========================================================
setlocal EnableExtensions EnableDelayedExpansion

set DRIVER=moufiltr.sys
set DST=%SystemRoot%\System32\drivers
set CLASS_KEY=HKLM\System\CurrentControlSet\Control\Class\{4D36E96F-E325-11CE-BFC1-08002BE10318}

echo.
echo === MouFiltr Installer ===

:: --- Check admin (do NOT auto-elevate) ---
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Please run this script as Administrator:
    echo     Right-click the .bat -> "Run as administrator".
    echo     The install cannot proceed without elevation.
    echo.
    pause
    exit /b 1
)

:: -------------------------
:: [1/4] Copy driver
:: -------------------------
echo [1/4] Copying %DRIVER% to %DST% ...
if not exist "%~dp0%DRIVER%" (
    echo [!] %DRIVER% not found next to this .bat
    echo     Put %DRIVER% in the same folder and run again.
    echo.
    pause
    exit /b 1
)
copy /y "%~dp0%DRIVER%" "%DST%\%DRIVER%" >nul
if %errorlevel%==0 (
    echo     OK
) else (
    echo [!] Copy failed. Check permissions or file lock.
    echo.
    pause
    exit /b 1
)

:: -------------------------
:: [2/4] Create or update service
:: -------------------------
echo [2/4] Creating/configuring service 'moufiltr' ...
sc query moufiltr >nul 2>&1
if %errorlevel%==0 (
    echo     Existing service found. Removing old version...
    sc stop moufiltr >nul 2>&1
    sc delete moufiltr >nul 2>&1
)
sc create moufiltr type= kernel start= demand error= normal ^
 binPath= \SystemRoot\System32\drivers\moufiltr.sys ^
 DisplayName= "Mouse Filter (moufiltr)" >nul
if %errorlevel%==0 (
    echo     OK
) else (
    echo [!] Failed to create service. Are you running as Administrator?
    echo.
    pause
    exit /b 1
)

:: -------------------------
:: [3/4] Update class UpperFilters (preserve others)
:: -------------------------
echo [3/4] Updating class UpperFilters (Mouse) ...
powershell -NoProfile -ExecutionPolicy Bypass ^
 "$k='HKLM:\System\CurrentControlSet\Control\Class\{4D36E96F-E325-11CE-BFC1-08002BE10318}';" ^
 "$v=(Get-ItemProperty -Path $k -Name UpperFilters -ErrorAction SilentlyContinue).UpperFilters;" ^
 "if($null -eq $v){$v=@()};" ^
 "$out=@('moufiltr'); foreach($x in $v){ if($x -and $x -ine 'moufiltr'){ $out+=$x } };" ^
 "Set-ItemProperty -Path $k -Name UpperFilters -Type MultiString -Value $out; Write-Host '    OK';"
if %errorlevel% neq 0 (
    echo [!] Registry update failed.
    echo.
    pause
    exit /b 1
)

:: -------------------------
:: [4/4] Final message
:: -------------------------
echo [4/4] Installation complete.
echo ---------------------------------------------
echo Replug the mouse OR disable/enable it in Device Manager
echo to rebuild the stack and activate the filter.
echo ---------------------------------------------
echo.
pause
endlocal
exit /b 0
