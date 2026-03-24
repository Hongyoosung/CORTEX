@echo off
setlocal EnableDelayedExpansion

REM ── Defaults ─────────────────────────────────────────────────────────────────
SET "EVAL_EPISODES=50"
SET "DETACH=false"
SET "NO_BUILD=false"
SET "LAUNCH_UE5=true"
SET "UE5_EXE=C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe"
SET "UPROJECT=C:\Users\PC\Documents\GitHub\DE\DE.uproject"
SET "SCHOLA_BASE_PORT=50051"
SET "NUM_SCHOLA_ENVS=2"
SET "POST_RESET_DELAY=4.0"

REM ── Argument parsing ──────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :end_parse
if "%~1"=="--episodes"  set "EVAL_EPISODES=%~2"  & shift & shift & goto :parse_args
if "%~1"=="--detach"    set "DETACH=true"         & shift           & goto :parse_args
if "%~1"=="--no-build"  set "NO_BUILD=true"       & shift           & goto :parse_args
if "%~1"=="--no-ue5"    set "LAUNCH_UE5=false"    & shift           & goto :parse_args
shift & goto :parse_args
:end_parse

cd /d "%~dp0.."

echo.
echo ========================================
echo  DE Live Evaluation
echo ========================================
echo  Episodes per checkpoint : %EVAL_EPISODES%
echo  UE5 environments        : %NUM_SCHOLA_ENVS% (fixed: best + latest)
echo ========================================

REM ── Docker sanity check ───────────────────────────────────────────────────────
docker info >nul 2>&1
if ERRORLEVEL 1 (
    echo ERROR: Docker is not running.
    pause
    exit /b 1
)

REM ── Select training run ───────────────────────────────────────────────────────
echo.
echo ----------------------------------------
echo  Available training runs:
echo ----------------------------------------

SET "RESULTS_DIR=%~dp0training_results"
if not exist "%RESULTS_DIR%" (
    echo ERROR: training_results\ does not exist.
    echo        Run training first to produce checkpoints.
    pause
    exit /b 1
)

SET "RUN_COUNT=0"
for /d %%D in ("%RESULTS_DIR%\*") do (
    set /a "RUN_COUNT+=1"
    set "RUN_!RUN_COUNT!=%%~nD"
    echo    !RUN_COUNT!^) %%~nD
)

if "%RUN_COUNT%"=="0" (
    echo No training runs found in training_results\.
    pause
    exit /b 1
)

echo.
set /p "RUN_CHOICE=Select run number (1-%RUN_COUNT%): "

SET "SELECTED_RUN="
for /l %%i in (1,1,%RUN_COUNT%) do (
    if "%RUN_CHOICE%"=="%%i" set "SELECTED_RUN=!RUN_%%i!"
)
if "%SELECTED_RUN%"=="" (
    echo Invalid selection.
    pause
    exit /b 1
)

SET "RUN_DIR=%RESULTS_DIR%\%SELECTED_RUN%"
echo.
echo Selected run: %SELECTED_RUN%

REM ── Locate best and latest checkpoints ───────────────────────────────────────
SET "HAS_BEST=false"
SET "HAS_LATEST=false"

if exist "%RUN_DIR%\best"   set "HAS_BEST=true"
if exist "%RUN_DIR%\latest" set "HAS_LATEST=true"

REM Fallback: if "latest" is missing, try the highest-numbered checkpoint_* dir
if "%HAS_LATEST%"=="false" (
    for /d %%D in ("%RUN_DIR%\checkpoint_*") do (
        set "FALLBACK_CKP=%%~nD"
    )
    if defined FALLBACK_CKP (
        if exist "%RUN_DIR%\!FALLBACK_CKP!" (
            echo    [NOTE] No 'latest' dir found — using !FALLBACK_CKP! as latest.
            set "HAS_LATEST=true"
            REM Create a temp symlink alias via xcopy is complex; just use the path directly
            set "LATEST_SUBDIR=!FALLBACK_CKP!"
        )
    )
) else (
    set "LATEST_SUBDIR=latest"
)

if "%HAS_BEST%"=="false" (
    echo ERROR: No 'best' checkpoint found in %RUN_DIR%
    echo        Training must complete at least one iteration with a reward improvement.
    pause
    exit /b 1
)
if "%HAS_LATEST%"=="false" (
    echo ERROR: No 'latest' or 'checkpoint_*' found in %RUN_DIR%
    pause
    exit /b 1
)

REM ── Build container-side paths (volume mounted at /app/training_results) ──────
SET "CHECKPOINT_BEST=/app/training_results/%SELECTED_RUN%/best"
SET "CHECKPOINT_LATEST=/app/training_results/%SELECTED_RUN%/%LATEST_SUBDIR%"

echo.
echo ----------------------------------------
echo  Checkpoints to evaluate:
echo    best   : %CHECKPOINT_BEST%
echo    latest : %CHECKPOINT_LATEST%
echo ----------------------------------------

REM ── Optional: allow user to override either checkpoint ───────────────────────
echo.
echo Press ENTER to use the defaults above, or type 'c' to customise checkpoints.
set /p "CUSTOM_CHOICE=Choice [ENTER / c]: "

if /i "%CUSTOM_CHOICE%"=="c" (
    echo.
    echo ----------------------------------------
    echo  Available checkpoints in %SELECTED_RUN%:
    echo ----------------------------------------
    SET "CKP_COUNT=0"
    for /d %%D in ("%RUN_DIR%\checkpoint_*") do (
        set /a "CKP_COUNT+=1"
        set "CKP_!CKP_COUNT!=%%~nD"
        echo    !CKP_COUNT!^) %%~nD
    )
    if exist "%RUN_DIR%\latest" (
        set /a "CKP_COUNT+=1"
        set "CKP_!CKP_COUNT!=latest"
        echo    !CKP_COUNT!^) latest
    )
    if exist "%RUN_DIR%\best" (
        set /a "CKP_COUNT+=1"
        set "CKP_!CKP_COUNT!=best"
        echo    !CKP_COUNT!^) best
    )
    echo.
    set /p "A_CHOICE=Select checkpoint A (env 0 / 'best' slot) [1-%CKP_COUNT%]: "
    set /p "B_CHOICE=Select checkpoint B (env 1 / 'latest' slot) [1-%CKP_COUNT%]: "

    SET "CKP_A=" & SET "CKP_B="
    for /l %%i in (1,1,%CKP_COUNT%) do (
        if "!A_CHOICE!"=="%%i" set "CKP_A=!CKP_%%i!"
        if "!B_CHOICE!"=="%%i" set "CKP_B=!CKP_%%i!"
    )
    if "!CKP_A!"=="" ( echo Invalid selection A. & pause & exit /b 1 )
    if "!CKP_B!"=="" ( echo Invalid selection B. & pause & exit /b 1 )

    SET "CHECKPOINT_BEST=/app/training_results/%SELECTED_RUN%/!CKP_A!"
    SET "CHECKPOINT_LATEST=/app/training_results/%SELECTED_RUN%/!CKP_B!"
    echo.
    echo  A (env 0) : !CHECKPOINT_BEST!
    echo  B (env 1) : !CHECKPOINT_LATEST!
)

REM ── Launch UE5 ────────────────────────────────────────────────────────────────
if "%LAUNCH_UE5%"=="true" (
    echo.
    echo [0/2] Launching UE5 with %NUM_SCHOLA_ENVS% Schola environments on port %SCHOLA_BASE_PORT%...
    echo       IMPORTANT: Make sure the level has bUseScriptedOpponent=true on both
    echo                  MatchManagers (Red = ScriptedAI, Blue = RL).
    if not exist "%UE5_EXE%" (
        echo ERROR: UE5 not found at: %UE5_EXE%
        pause
        exit /b 1
    )
    start "" "%UE5_EXE%" "%UPROJECT%" -game -nullrhi -nosound -notexturestreaming -nopause -ScholaPort=%SCHOLA_BASE_PORT%~%NUM_SCHOLA_ENVS% -log
    echo Waiting 3s for UE5 to initialise...
    timeout /t 3 /nobreak >nul
    echo UE5 ready.
    echo.
)

REM ── Build Docker image ────────────────────────────────────────────────────────
if "%NO_BUILD%"=="false" (
    echo [1/2] Building evaluation image...
    docker compose --profile eval build eval
    if ERRORLEVEL 1 (
        echo ERROR: Image build failed.
        pause
        exit /b 1
    )
    echo.
)

REM ── Run evaluation container ──────────────────────────────────────────────────
echo [2/2] Starting evaluation container...
echo.

if "%DETACH%"=="true" (
    docker compose --profile eval up -d eval
    if ERRORLEVEL 1 (
        echo ERROR: Failed to start container.
        pause
        exit /b 1
    )
    docker compose logs -f eval
) else (
    docker compose --profile eval up eval
    if ERRORLEVEL 1 (
        echo ERROR: Evaluation exited with an error.
        pause
        exit /b 1
    )
)

echo.
echo Evaluation finished.
echo Results are saved in: DE_Training\training_results\%SELECTED_RUN%\eval_results_*.json
echo.
pause
