@echo off
setlocal

:: ============================================================
::  SecureStorage_rev1 — development helper
::  Usage:  run.bat [command]
::
::  Commands:
::    server      Start telemetry server (new window)
::    uart        Open UART monitor on COM6 (new window)
::    ping        Continuous ping to board (new window)
::    monitor     server + uart + ping (three windows)
::    flash       Build & flash Boot + Appli
::    flash-boot  Flash Boot only
::    flash-appli Flash Appli only
::    run         flash + monitor  (full workflow)
::    help        Show this message
:: ============================================================

:: ---------- config -------------------------------------------
set UART_PORT=COM6
set UART_BAUD=115200
set BOARD_IP=192.168.0.100
set SERVER_PY=server\server.py

set PROGRAMMER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe
set EXT_LOADER=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\ExternalLoader\MX25UW25645G_NUCLEO-H7S3L8.stldr
set MAKE=C:\ST\STM32CubeIDE_1.13.2\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe

set BOOT_ELF=build\Boot\SecureStorage_rev1_Boot.elf
set APPLI_ELF=build\Appli\SecureStorage_rev1_Appli.elf
:: -------------------------------------------------------------

if "%~1"==""        goto :menu
if /i "%~1"=="server"      goto :server
if /i "%~1"=="uart"        goto :uart
if /i "%~1"=="ping"        goto :ping
if /i "%~1"=="monitor"     goto :monitor
if /i "%~1"=="flash"       goto :flash
if /i "%~1"=="flash-boot"  goto :flash_boot
if /i "%~1"=="flash-appli" goto :flash_appli
if /i "%~1"=="run"         goto :run
if /i "%~1"=="help"        goto :help
echo [error] Unknown command: %~1
goto :help

:: ============================================================
:menu
echo.
echo  SecureStorage_rev1 — run.bat
echo  ============================================================
echo   1  server       Start telemetry server  (new window)
echo   2  uart         UART monitor COM6        (new window)
echo   3  ping         Ping board %BOARD_IP%  (new window)
echo   4  monitor      All three windows at once
echo   5  flash        Build + flash Boot + Appli
echo   6  flash-boot   Flash Boot only
echo   7  flash-appli  Flash Appli only
echo   8  run          flash + monitor  (full workflow)
echo   9  help         Show usage
echo   0  exit
echo  ============================================================
set /p CHOICE= Choose:
if "%CHOICE%"=="1" goto :server
if "%CHOICE%"=="2" goto :uart
if "%CHOICE%"=="3" goto :ping
if "%CHOICE%"=="4" goto :monitor
if "%CHOICE%"=="5" goto :flash
if "%CHOICE%"=="6" goto :flash_boot
if "%CHOICE%"=="7" goto :flash_appli
if "%CHOICE%"=="8" goto :run
if "%CHOICE%"=="9" goto :help
if "%CHOICE%"=="0" goto :eof
echo [error] Invalid choice.
goto :menu

:: ============================================================
:server
echo [server] Starting telemetry server...
start "Telemetry Server :5000" cmd /k "python %SERVER_PY%"
goto :eof

:: ============================================================
:uart
echo [uart] Opening %UART_PORT% @ %UART_BAUD% baud...
start "UART %UART_PORT%" cmd /k "python -m serial.tools.miniterm %UART_PORT% %UART_BAUD% --raw --eol LF"
goto :eof

:: ============================================================
:ping
echo [ping] Pinging %BOARD_IP%...
start "Ping %BOARD_IP%" cmd /k "ping -t %BOARD_IP%"
goto :eof

:: ============================================================
:monitor
call :server
call :uart
call :ping
echo [monitor] Server + UART + Ping windows launched.
goto :eof

:: ============================================================
:flash
echo [build] Building Boot + Appli...
"%MAKE%" all
if errorlevel 1 (
    echo [error] Build failed. Aborting flash.
    pause
    goto :eof
)
call :flash_boot_impl
call :flash_appli_impl
echo [flash] Done.
goto :eof

:flash_boot
echo [build] Building Boot...
"%MAKE%" boot
if errorlevel 1 (
    echo [error] Build failed.
    pause
    goto :eof
)
call :flash_boot_impl
goto :eof

:flash_appli
echo [build] Building Appli...
"%MAKE%" appli
if errorlevel 1 (
    echo [error] Build failed.
    pause
    goto :eof
)
call :flash_appli_impl
goto :eof

:: ============================================================
:run
call :flash
if errorlevel 1 goto :eof
call :monitor
echo [run] Board flashed and monitoring started.
goto :eof

:: ============================================================
:help
echo.
echo  Usage:  run.bat [command]
echo.
echo  Commands:
echo    server      Start telemetry server in a new window
echo    uart        Open UART monitor (COM6 115200) in a new window
echo    ping        Continuous ping to %BOARD_IP% in a new window
echo    monitor     Open server + uart + ping windows at once
echo    flash       Build and flash Boot + Appli
echo    flash-boot  Build and flash Boot only
echo    flash-appli Build and flash Appli only
echo    run         Build + flash + monitor  (full workflow)
echo    help        Show this message
echo.
echo  Run without arguments for interactive menu.
goto :eof

:: ============================================================
:: Internal flash helpers (no build step — called after build)
:flash_boot_impl
echo [flash] Boot -^> internal flash...
"%PROGRAMMER%" -c port=SWD freq=8000 -w %BOOT_ELF% -hardRst
if errorlevel 1 echo [error] Boot flash failed.
goto :eof

:flash_appli_impl
echo [flash] Appli -^> XSPI2 NOR flash...
"%PROGRAMMER%" -c port=SWD freq=8000 -el "%EXT_LOADER%" -w %APPLI_ELF% -hardRst
if errorlevel 1 echo [error] Appli flash failed.
goto :eof
