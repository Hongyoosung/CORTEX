@echo off
REM ============================================================================
REM Complete Training Session Launcher
REM
REM Starts both UE5 headless server and Python training script.
REM Designed for overnight/multi-day training runs.
REM ============================================================================

setlocal

echo ============================================================================
echo MOC v10.1 Phase 1 Training Session
echo ============================================================================
echo.

REM === Configuration ===
SET PYTHON_ENV=%~dp0venv\Scripts\python.exe
SET TRAINING_SCRIPT=%~dp0training\phase1_policy_training.py

REM Check if virtual environment exists
if not exist "%PYTHON_ENV%" (
    echo ERROR: Python virtual environment not found.
    echo Please run: python -m venv venv
    echo Then install dependencies: venv\Scripts\pip install -r requirements.txt
    pause
    exit /b 1
)

REM === Start UE5 headless server in background ===
echo [1/2] Launching UE5 headless server...
start "UE5 Training Server" cmd /c launch_headless_training.bat

REM Wait for server to initialize
echo Waiting 30 seconds for server initialization...
timeout /t 30 /nobreak

REM === Start Python training script ===
echo.
echo [2/2] Starting Python training pipeline...
echo.

%PYTHON_ENV% %TRAINING_SCRIPT% ^
    --host localhost ^
    --port 8888 ^
    --transitions 100000 ^
    --batch-size 256 ^
    --device cuda

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Training script failed with error code %ERRORLEVEL%
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ============================================================================
echo Training session completed successfully!
echo ============================================================================
pause
