@echo off
REM v6.0 Training Pipeline for Windows
REM Ensures config is synced from C++ before training

echo ===================================
echo CORTEX v6.0 Training Pipeline
echo ===================================
echo.

REM Step 1: Sync configuration from C++
echo [1/2] Syncing config from C++...
python tools\sync_config_from_cpp.py
if errorlevel 1 (
    echo.
    echo [ERROR] Config sync failed! Training aborted.
    echo Please check that RLTypes.h exists and is valid.
    exit /b 1
)
echo.

REM Step 2: Start training
echo [2/2] Starting training...
echo Training arguments: %*
echo.

REM Check if specific training script was specified
if "%1"=="" (
    echo Using default training script: train_rllib.py
    python train_rllib.py
) else (
    python %*
)

if errorlevel 1 (
    echo.
    echo [ERROR] Training failed!
    exit /b 1
)

echo.
echo ===================================
echo Training completed successfully!
echo ===================================
