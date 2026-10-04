@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "CLI=%~dp0arduino-cli.exe"
set "DATA=%~dp0.arduino-data"
set "DOWNLOADS=%~dp0.arduino-downloads"
set "USERDIR=%~dp0.arduino-user"
set "HEX=%~dp0PSN-Presepe.ino.hex"

echo.
echo ==========================================
echo   PSN-Presepe - Flash Arduino Mega 2560
echo ==========================================
echo.

if not exist "%HEX%" (
  echo ERRORE: non trovo PSN-Presepe.ino.hex
  echo Metti il file HEX nella stessa cartella di questo BAT.
  pause
  exit /b 1
)

if not exist "%CLI%" (
  echo Arduino CLI non presente. Download della versione ufficiale...
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Invoke-WebRequest -UseBasicParsing 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip' -OutFile '%TEMP%\arduino-cli.zip'; Expand-Archive -Force '%TEMP%\arduino-cli.zip' '%~dp0.cli-tmp'; Copy-Item -Force '%~dp0.cli-tmp\arduino-cli.exe' '%CLI%'; Remove-Item -Recurse -Force '%~dp0.cli-tmp'; Remove-Item -Force '%TEMP%\arduino-cli.zip'"
  if errorlevel 1 (
    echo ERRORE durante il download di Arduino CLI.
    pause
    exit /b 1
  )
)

if not exist "%DATA%\packages\arduino\hardware\avr" (
  echo Prima inizializzazione: installazione supporto Arduino AVR...
  "%CLI%" config init --overwrite --config-file "%~dp0arduino-cli.yaml" >nul
  "%CLI%" config set directories.data "%DATA%" --config-file "%~dp0arduino-cli.yaml"
  "%CLI%" config set directories.downloads "%DOWNLOADS%" --config-file "%~dp0arduino-cli.yaml"
  "%CLI%" config set directories.user "%USERDIR%" --config-file "%~dp0arduino-cli.yaml"
  "%CLI%" core update-index --config-file "%~dp0arduino-cli.yaml"
  "%CLI%" core install arduino:avr --config-file "%~dp0arduino-cli.yaml"
  if errorlevel 1 (
    echo ERRORE durante l'installazione del supporto AVR.
    pause
    exit /b 1
  )
)

set "PORT="
for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "$j = & '%CLI%' board list --format json --config-file '%~dp0arduino-cli.yaml' | ConvertFrom-Json; $p = $j.detected_ports | Where-Object { $_.matching_boards.fqbn -contains 'arduino:avr:mega' } | Select-Object -First 1; if ($p) { $p.port.address }"`) do set "PORT=%%P"

if not defined PORT (
  echo.
  echo Arduino Mega non rilevata automaticamente.
  set /p "PORT=Inserisci la porta COM ^(es. COM3^): "
)

if not defined PORT (
  echo ERRORE: porta COM non specificata.
  pause
  exit /b 1
)

echo.
echo Scheda : Arduino Mega 2560
echo Porta  : %PORT%
echo Firmware: %HEX%
echo.
echo Caricamento in corso...

"%CLI%" upload -p "%PORT%" --fqbn arduino:avr:mega --input-dir "%~dp0" --config-file "%~dp0arduino-cli.yaml" --verify
if errorlevel 1 (
  echo.
  echo FLASH FALLITO.
  echo Controlla la porta COM e che nessun altro programma la stia usando.
  pause
  exit /b 1
)

echo.
echo ==========================================
echo   FLASH COMPLETATO CON SUCCESSO
echo ==========================================
pause
