@echo off
REM ============================================================================
REM Phase 1 Policy Training - v10.2 Commander-Executor Architecture
REM
REM Launches the v10.2 policy training container via Docker Compose and
REM streams its log to this console.
REM
REM Usage:
REM   phase1_training.bat [options]
REM
REM Options:
REM   --envs N         Number of Schola UE5 environments  (default: 4)
REM   --workers N      Number of Ray RLlib env-runners     (default: 0, local)
REM   --iterations N   Number of training iterations       (default: 100)
REM   --detach         Start container in background then follow its log
REM   --no-build       Skip image rebuild (faster relaunch)
REM
REM Environment variables are exported to Docker Compose and read by
REM phase1_policy_training_v10_2.py via MOCv10_2TrainingConfig.
REM ============================================================================

setlocal EnableDelayedExpansion

REM ── Default configuration ────────────────────────────────────────────────────
SET NUM_SCHOLA_ENVS=4
SET NUM_WORKERS=0
SET NUM_ITERATIONS=100
SET DETACH=false
SET NO_BUILD=false

REM ── Parse arguments ──────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :end_parse

if "%~1"=="--envs" (
    set NUM_SCHOLA_ENVS=%~2
    shift & shift & goto :parse_args
)
if "%~1"=="--workers" (
    set NUM_WORKERS=%~2
    shift & shift & goto :parse_args
)
if "%~1"=="--iterations" (
    set NUM_ITERATIONS=%~2
    shift & shift & goto :parse_args
)
if "%~1"=="--detach" (
    set DETACH=true
    shift & goto :parse_args
)
if "%~1"=="--no-build" (
    set NO_BUILD=true
    shift & goto :parse_args
)
echo WARNING: Unknown argument "%~1" (ignored)
shift & goto :parse_args
:end_parse

REM ── Change to the script's own directory so Docker Compose finds the files ──
cd /d "%~dp0"

REM ── Print configuration ──────────────────────────────────────────────────────
echo.
echo ============================================================================
echo  MOC v10.2  Phase 1 Policy Training
echo ============================================================================
echo   Schola UE5 environments : %NUM_SCHOLA_ENVS%
echo   Ray RLlib workers       : %NUM_WORKERS%
echo   Training iterations     : %NUM_ITERATIONS%
echo   Detach mode             : %DETACH%
echo   Skip build              : %NO_BUILD%
echo ============================================================================
echo.

REM ── Validate Docker is running ───────────────────────────────────────────────
docker info >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Docker is not running or not installed.
    echo Please start Docker Desktop and try again.
    pause
    exit /b 1
)

REM ── Export vars so Docker Compose picks them up via ${VAR:-default} syntax ──
REM    These three are the only values docker-compose.yml interpolates from host.
SET NUM_SCHOLA_ENVS=%NUM_SCHOLA_ENVS%
SET NUM_WORKERS=%NUM_WORKERS%
SET NUM_ITERATIONS=%NUM_ITERATIONS%

REM ── Optionally rebuild image ─────────────────────────────────────────────────
if "%NO_BUILD%"=="false" (
    echo [1/2] Building training image...
    docker compose --profile v10.2 build training-v10.2
    if !ERRORLEVEL! NEQ 0 (
        echo ERROR: Image build failed.
        pause
        exit /b !ERRORLEVEL!
    )
    echo.
)

REM ── Launch container ─────────────────────────────────────────────────────────
if "%DETACH%"=="true" (
    echo [2/2] Starting container in background...
    docker compose --profile v10.2 up -d training-v10.2
    if !ERRORLEVEL! NEQ 0 (
        echo ERROR: Failed to start container.
        pause
        exit /b !ERRORLEVEL!
    )
    echo.
    echo Container started. Following log output (Ctrl+C stops log tail only):
    echo.
    docker compose logs -f training-v10.2
) else (
    echo [2/2] Starting container (Ctrl+C stops training)...
    echo.
    docker compose --profile v10.2 up training-v10.2
    SET EXIT_CODE=!ERRORLEVEL!
    if !EXIT_CODE! NEQ 0 (
        echo.
        echo ERROR: Training container exited with code !EXIT_CODE!
        echo Check logs above or run:
        echo   docker compose logs training-v10.2
        pause
        exit /b !EXIT_CODE!
    )
)

echo.
echo ============================================================================
echo  Training finished. Results saved to: training_results_v10_2\
echo ============================================================================
pause
