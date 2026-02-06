@echo off
REM ============================================================================
REM Headless UE5 Training Launcher
REM
REM Launches UE5 in headless mode for overnight training sessions.
REM Configures rendering settings for minimal overhead and maximum performance.
REM
REM Usage:
REM   launch_headless_training.bat [--map MapName] [--duration Hours]
REM
REM Examples:
REM   launch_headless_training.bat
REM   launch_headless_training.bat --map MOC_Arena --duration 8
REM ============================================================================

setlocal

REM === Configuration ===
SET PROJECT_DIR=C:\Users\PC\Documents\GitHub\CORTEX
SET PROJECT_FILE=%PROJECT_DIR%\CORTEX.uproject
SET UE5_EDITOR="C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe"

REM Default values
SET MAP_NAME=MOC_Arena
SET TRAINING_DURATION=12

REM Parse arguments
:parse_args
if "%~1"=="" goto :end_parse
if "%~1"=="--map" (
    set MAP_NAME=%~2
    shift
    shift
    goto :parse_args
)
if "%~1"=="--duration" (
    set TRAINING_DURATION=%~2
    shift
    shift
    goto :parse_args
)
shift
goto :parse_args
:end_parse

echo ============================================================================
echo MOC v10.1 Headless Training Session
echo ============================================================================
echo Project: %PROJECT_FILE%
echo Map: %MAP_NAME%
echo Training Duration: %TRAINING_DURATION% hours
echo ============================================================================
echo.

REM === Validate project file exists ===
if not exist "%PROJECT_FILE%" (
    echo ERROR: Project file not found: %PROJECT_FILE%
    pause
    exit /b 1
)

REM === Create log directory ===
SET LOG_DIR=%PROJECT_DIR%\MOC_Training\training_logs
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

REM === Generate timestamp for log file ===
for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /format:list') do set datetime=%%I
set LOG_FILE=%LOG_DIR%\training_%datetime:~0,8%_%datetime:~8,6%.log

echo Starting headless UE5 server...
echo Log file: %LOG_FILE%
echo.

REM === Launch UE5 in headless mode ===
REM Command-line arguments:
REM   -game                   : Run as game (not editor)
REM   -server                 : Run as dedicated server
REM   -log                    : Enable logging
REM   -unattended             : No user interaction
REM   -nullrhi                : Null rendering (no GPU rendering)
REM   -nosplash               : Skip splash screen
REM   -nosound                : Disable audio
REM   -NoScreenMessages       : Disable on-screen messages
REM   -ExecCmds="..."         : Execute console commands on startup

%UE5_EDITOR% ^
    "%PROJECT_FILE%" ^
    %MAP_NAME% ^
    -game ^
    -server ^
    -log ^
    -unattended ^
    -nullrhi ^
    -nosplash ^
    -nosound ^
    -NoScreenMessages ^
    -ExecCmds="t.MaxFPS 60" ^
    -ExecCmds="r.SetRes 1280x720w" ^
    -ExecCmds="sg.ViewDistanceQuality 0" ^
    -ExecCmds="sg.ShadowQuality 0" ^
    -ExecCmds="sg.TextureQuality 0" ^
    -ExecCmds="sg.EffectsQuality 0" ^
    -ExecCmds="sg.PostProcessQuality 0" ^
    > "%LOG_FILE%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: UE5 server crashed with error code %ERRORLEVEL%
    echo Check log file: %LOG_FILE%
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Training session completed successfully.
echo Log file saved: %LOG_FILE%
pause
