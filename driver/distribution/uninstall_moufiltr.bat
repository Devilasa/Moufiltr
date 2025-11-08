@echo off
setlocal EnableExtensions

>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"
if %errorlevel% neq 0 (
  echo [!] Run this script as Administrator.
  pause
  exit /b 1
)

set DRIVER=moufiltr.sys
set DST=%SystemRoot%\System32\drivers
set CLASS_KEY=HKLM\System\CurrentControlSet\Control\Class\{4D36E96F-E325-11CE-BFC1-08002BE10318}

echo.
echo === MouFiltr uninstaller ===

echo [1/3] Removing 'moufiltr' from class UpperFilters ...
powershell -NoProfile -Command ^
  "$k='HKLM:\System\CurrentControlSet\Control\Class\{4D36E96F-E325-11CE-BFC1-08002BE10318}';" ^
  "$v=(Get-ItemProperty -Path $k -Name UpperFilters -ErrorAction SilentlyContinue).UpperFilters;" ^
  "if($null -ne $v){" ^
  "  $out=@(); foreach($x in $v){ if($x -and $x -ine 'moufiltr'){ $out+=$x } };" ^
  "  if($out.Count -eq 0){ Remove-ItemProperty -Path $k -Name UpperFilters -ErrorAction SilentlyContinue }" ^
  "  else { Set-ItemProperty -Path $k -Name UpperFilters -Type MultiString -Value $out }" ^
  "}"

echo [2/3] Deleting service 'moufiltr' ...
sc stop moufiltr >nul 2>&1
sc delete moufiltr >nul 2>&1

echo [3/3] Deleting driver file (optional) ...
del /f /q "%DST%\%DRIVER%" >nul 2>&1

echo Done. Replug/disable-enable the mouse to rebuild the stack.
pause
exit /b 0
