@echo off
setlocal EnableDelayedExpansion

SET "NUM_SCHOLA_ENVS=4"
SET "NUM_WORKERS=0"
SET "NUM_ITERATIONS=50"
SET "DETACH=false"
SET "NO_BUILD=false"

:parse_args
if "%~1"=="" goto :end_parse
if "%~1"=="--envs" set "NUM_SCHOLA_ENVS=%~2" & shift & shift & goto :parse_args
if "%~1"=="--workers" set "NUM_WORKERS=%~2" & shift & shift & goto :parse_args
if "%~1"=="--iterations" set "NUM_ITERATIONS=%~2" & shift & shift & goto :parse_args
if "%~1"=="--detach" set "DETACH=true" & shift & goto :parse_args
if "%~1"=="--no-build" set "NO_BUILD=true" & shift & goto :parse_args
shift & goto :parse_args
:end_parse

cd /d "%~dp0"

echo.
echo ========================================
echo  MOC v10.2 Phase 1 Policy Training
echo ========================================
echo  Envs: %NUM_SCHOLA_ENVS% / Workers: %NUM_WORKERS%
echo ========================================

docker info >nul 2>&1
if ERRORLEVEL 1 (
    echo ERROR: Docker is not running.
    pause
    exit /b 1
)

if "%NO_BUILD%"=="false" (
    echo [1/2] Building training image...
    docker compose --profile v10.2 build training-v10.2
    if ERRORLEVEL 1 (
        echo ERROR: Image build failed.
        pause
        exit /b 1
    )
    echo.
)

if "%DETACH%"=="true" (
    echo [2/2] Starting container in background...
    docker compose --profile v10.2 up -d training-v10.2
    if ERRORLEVEL 1 (
        echo ERROR: Failed to start container.
        pause
        exit /b 1
    )
    docker compose logs -f training-v10.2
) else (
    echo [2/2] Starting container...
    docker compose --profile v10.2 up training-v10.2
    if ERRORLEVEL 1 (
        echo ERROR: Training exited with error.
        pause
        exit /b 1
    )
)

echo.
echo Training finished.
pause