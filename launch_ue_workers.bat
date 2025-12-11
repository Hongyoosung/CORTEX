@echo off
REM Launch 4 UE5 instances for distributed RL training
REM
REM MODES:
REM   1. Packaged Game (RECOMMENDED) - Fast startup, low memory
REM   2. Editor Mode - Slower, more memory, useful for debugging
REM
REM To package the game:
REM   1. Open project in UE5 Editor
REM   2. Platforms > Windows > Package Project
REM   3. Select output folder (default: WindowsNoEditor/)
REM   4. Wait for build to complete (~5-10 min)
REM   5. Update GAME_PATH below to point to .exe

REM ========================================
REM CONFIGURATION
REM ========================================

REM MODE 1: Packaged Game (RECOMMENDED - 2-3x faster, 50% less RAM)
set GAME_PATH=C:\Users\Foryoucom\Documents\GitHub\4d\SBDAPM\WindowsNoEditor\GameAI_Project.exe

REM MODE 2: Editor Mode (fallback for debugging)
set EDITOR_PATH=C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe
set PROJECT=C:\Users\Foryoucom\Documents\GitHub\4d\SBDAPM\GameAI_Project.uproject

REM Check if packaged game exists
if exist "%GAME_PATH%" (
    set "LAUNCH_MODE=GAME"
    set "LAUNCH_PATH=%GAME_PATH%"
    set "EXTRA_ARGS="
    echo [INFO] Using PACKAGED GAME mode - faster and more efficient
) else (
    set "LAUNCH_MODE=EDITOR"
    set "LAUNCH_PATH=%EDITOR_PATH%"
    set "EXTRA_ARGS=%PROJECT%"
    echo [WARNING] Packaged game not found, falling back to EDITOR mode
    echo [INFO] For better performance, package your game first
)

echo ========================================
echo Launching 4 UE5 Workers for RL Training
echo Mode: %LAUNCH_MODE%
echo ========================================
echo.

REM Worker 0 (Port 50051)
echo [1/4] Launching Worker 0 on port 50051...
start "UE Worker 0" "%LAUNCH_PATH%" %EXTRA_ARGS% -ScholaPort=50051 -NoSound -WINDOWED -ResX=800 -ResY=600 -WinX=0 -WinY=0
timeout /t 5 /nobreak

REM Worker 1 (Port 50052)
echo [2/4] Launching Worker 1 on port 50052...
start "UE Worker 1" "%LAUNCH_PATH%" %EXTRA_ARGS% -ScholaPort=50052 -NoSound -WINDOWED -ResX=800 -ResY=600 -WinX=810 -WinY=0
timeout /t 5 /nobreak

REM Worker 2 (Port 50053)
echo [3/4] Launching Worker 2 on port 50053...
start "UE Worker 2" "%LAUNCH_PATH%" %EXTRA_ARGS% -ScholaPort=50053 -NoSound -WINDOWED -ResX=800 -ResY=600 -WinX=0 -WinY=650
timeout /t 5 /nobreak

REM Worker 3 (Port 50054)
echo [4/4] Launching Worker 3 on port 50054...
start "UE Worker 3" "%LAUNCH_PATH%" %EXTRA_ARGS% -ScholaPort=50054 -NoSound -WINDOWED -ResX=800 -ResY=600 -WinX=810 -WinY=650

echo.
echo ========================================
echo All 4 workers launched!
echo ========================================
echo.
if "%LAUNCH_MODE%"=="GAME" (
    echo Startup time: ~15-20 seconds ^(packaged game^)
) else (
    echo Startup time: ~30-60 seconds ^(editor mode^)
)
echo.
echo Next steps:
echo   1. Wait for all windows to finish loading
echo   2. Run: python train_rllib.py
echo.
pause
