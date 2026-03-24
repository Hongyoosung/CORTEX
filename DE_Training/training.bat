@echo off
setlocal EnableDelayedExpansion

SET "NUM_SCHOLA_ENVS=8"
SET "NUM_WORKERS=0"
SET "NUM_ITERATIONS=150"
SET "DETACH=false"
SET "NO_BUILD=false"
SET "RESUME_CHECKPOINT="
SET "UE5_EXE=C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe"
SET "UPROJECT=C:\Users\Foryoucom\Documents\GitHub\CORTEX\DE.uproject"
SET "SCHOLA_BASE_PORT=50051"
SET "LAUNCH_UE5=true"

:parse_args
if "%~1"=="" goto :end_parse
if "%~1"=="--envs" set "NUM_SCHOLA_ENVS=%~2" & shift & shift & goto :parse_args
if "%~1"=="--workers" set "NUM_WORKERS=%~2" & shift & shift & goto :parse_args
if "%~1"=="--iterations" set "NUM_ITERATIONS=%~2" & shift & shift & goto :parse_args
if "%~1"=="--detach" set "DETACH=true" & shift & goto :parse_args
if "%~1"=="--no-build" set "NO_BUILD=true" & shift & goto :parse_args
if "%~1"=="--no-ue5" set "LAUNCH_UE5=false" & shift & goto :parse_args
shift & goto :parse_args
:end_parse

cd /d "%~dp0.."

echo.
echo ========================================
echo  DE Policy Training
echo ========================================
echo  Envs: %NUM_SCHOLA_ENVS% / Workers: %NUM_WORKERS%
echo ========================================

docker info >nul 2>&1
if ERRORLEVEL 1 (
    echo ERROR: Docker is not running.
    pause
    exit /b 1
)

REM ── Mode selection ──────────────────────────────────────────────────────────
echo.
echo Select training mode:
echo   1) Start new training
echo   2) Resume from checkpoint
echo.
set /p "MODE_CHOICE=Enter choice (1 or 2): "

if "%MODE_CHOICE%"=="2" goto :resume_menu
if "%MODE_CHOICE%"=="1" goto :start_training
echo Invalid choice. Defaulting to new training.
goto :start_training


REM ── Resume menu ─────────────────────────────────────────────────────────────
:resume_menu
echo.
echo ----------------------------------------
echo  Available training runs:
echo ----------------------------------------

REM List run directories under training_results\
set "RESULTS_DIR=%~dp0training_results"
if not exist "%RESULTS_DIR%" (
    echo No training runs found. training_results\ does not exist.
    echo Falling back to new training.
    goto :start_training
)

set "RUN_COUNT=0"
for /d %%D in ("%RESULTS_DIR%\*") do (
    set /a "RUN_COUNT+=1"
    set "RUN_!RUN_COUNT!=%%~nD"
    echo    !RUN_COUNT!^) %%~nD
)

if "%RUN_COUNT%"=="0" (
    echo No training runs found in training_results\.
    echo Falling back to new training.
    goto :start_training
)

echo.
set /p "RUN_CHOICE=Select run number (1-%RUN_COUNT%): "

REM Validate run choice
set "SELECTED_RUN="
for /l %%i in (1,1,%RUN_COUNT%) do (
    if "%RUN_CHOICE%"=="%%i" set "SELECTED_RUN=!RUN_%%i!"
)
if "%SELECTED_RUN%"=="" (
    echo Invalid selection. Falling back to new training.
    goto :start_training
)

echo.
echo ----------------------------------------
echo  Checkpoints in %SELECTED_RUN%:
echo ----------------------------------------

set "RUN_DIR=%RESULTS_DIR%\%SELECTED_RUN%"
set "CKP_COUNT=0"
set "CKP_LATEST="

REM List numbered checkpoints (alphabetical = numerical order for checkpoint_XXXXXX)
for /d %%D in ("%RUN_DIR%\checkpoint_*") do (
    set /a "CKP_COUNT+=1"
    set "CKP_!CKP_COUNT!=%%~nD"
    set "CKP_LATEST=%%~nD"
    echo    !CKP_COUNT!^) %%~nD
)

REM Add "latest" shortcut (dedicated overwriting checkpoint saved every iteration)
if exist "%RUN_DIR%\latest" (
    set /a "CKP_COUNT+=1"
    set "CKP_!CKP_COUNT!=latest"
    echo    !CKP_COUNT!^) latest  ^(most recent progress^)
) else if defined CKP_LATEST (
    REM Fallback: no dedicated latest/ dir, use highest-numbered checkpoint
    set /a "CKP_COUNT+=1"
    set "CKP_!CKP_COUNT!=!CKP_LATEST!"
    echo    !CKP_COUNT!^) latest  ^(!CKP_LATEST!^)
)

REM Add "best" if it exists
if exist "%RUN_DIR%\best" (
    set /a "CKP_COUNT+=1"
    set "CKP_!CKP_COUNT!=best"
    echo    !CKP_COUNT!^) best  ^(highest-reward checkpoint^)
)

if "!CKP_COUNT!"=="0" (
    echo No checkpoints found in %SELECTED_RUN%.
    echo Falling back to new training.
    goto :start_training
)

echo.
set /p "CKP_CHOICE=Select checkpoint number (1-%CKP_COUNT%): "

set "SELECTED_CKP="
for /l %%i in (1,1,%CKP_COUNT%) do (
    if "%CKP_CHOICE%"=="%%i" set "SELECTED_CKP=!CKP_%%i!"
)
if "%SELECTED_CKP%"=="" (
    echo Invalid selection. Falling back to new training.
    goto :start_training
)

REM Build the container-side path (volume is mounted at /app/training_results)
set "RESUME_CHECKPOINT=/app/training_results/%SELECTED_RUN%/%SELECTED_CKP%"
echo.
echo Resuming from: %RESUME_CHECKPOINT%
goto :start_training


REM ── Build and run ────────────────────────────────────────────────────────────
:start_training
echo.
echo ========================================
if "%RESUME_CHECKPOINT%"=="" (
    echo  Starting new training run
) else (
    echo  Resuming training
    echo  Checkpoint: %SELECTED_CKP%
)
echo ========================================

REM ── Launch UE5 ───────────────────────────────────────────────────────────────
if "%LAUNCH_UE5%"=="true" (
    echo [0/2] Launching UE5 with %NUM_SCHOLA_ENVS% Schola environments on port %SCHOLA_BASE_PORT%...
    if not exist "%UE5_EXE%" (
        echo ERROR: UE5 not found at: %UE5_EXE%
        pause
        exit /b 1
    )
    start "" "%UE5_EXE%" "%UPROJECT%" -game -nullrhi -nosound -notexturestreaming -nopause -ScholaPort=%SCHOLA_BASE_PORT%~%NUM_SCHOLA_ENVS% -log
    echo Waiting 3s for UE5 to initialize...
    timeout /t 3 /nobreak >nul
    echo UE5 ready.
    echo.
)

if "%NO_BUILD%"=="false" (
    echo [1/2] Building training image...
    docker compose --profile policy build training
    if ERRORLEVEL 1 (
        echo ERROR: Image build failed.
        pause
        exit /b 1
    )
    echo.
)

if "%DETACH%"=="true" (
    echo [2/2] Starting container in background...
    docker compose --profile policy up -d training
    if ERRORLEVEL 1 (
        echo ERROR: Failed to start container.
        pause
        exit /b 1
    )
    docker compose logs -f training
) else (
    echo [2/2] Starting container...
    docker compose --profile policy up training
    if ERRORLEVEL 1 (
        echo ERROR: Training exited with error.
        pause
        exit /b 1
    )
)

echo.
echo Training finished.
pause
