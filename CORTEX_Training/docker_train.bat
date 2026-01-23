@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ================================================================================
echo CORTEX v8.0 Docker Training Launcher
echo ================================================================================
echo.

REM [1/4] Docker check
docker info 1>nul 2>nul
if not %errorlevel%==0 (
    echo ERROR: Docker is not running!
    echo Please start Docker Desktop and try again.
    pause
    exit /b 1
)
echo [1/4] Docker is running
echo.

REM [2/4] Image check and build
docker images cortex_training-training-single 2>nul | findstr cortex_training-training-single 1>nul 2>nul
if not %errorlevel%==0 (
    echo [2/4] Building Docker image...
    docker-compose build training-single
    if not %errorlevel%==0 (
        echo ERROR: Docker build failed!
        pause
        exit /b 1
    )
) else (
    echo [2/4] Docker image already built
)
echo.

REM [3/4] Connection test
echo [3/4] Testing connection to UE5...
echo.
echo [3a] TCP connection test...
docker-compose run --rm test-connection
if not %errorlevel%==0 (
    echo ERROR: TCP connection failed!
    echo 1. UE5 is running? 2. Press Play? 3. Check Port 50051?
    pause
    exit /b 1
)
echo.
echo [3b] gRPC connection test...
docker-compose run --rm test-grpc
if not %errorlevel%==0 (
    echo WARNING: gRPC test failed (may still work with retries)
    echo If training fails, check UE5 Schola configuration
    timeout /t 3 /nobreak
)
echo.

REM [4/4] Training mode selection
echo [4/4] Select training mode...
echo.
echo ================================================================================
echo Training Options
echo ================================================================================
echo.
echo 1. Single-instance training (NUM_WORKERS=0, runs in main process)
echo    - Simplest setup, single UE5 instance
echo    - Best for initial testing and small-scale training
echo.
echo 2. Multi-instance training (NUM_WORKERS=N, parallel environments)
echo    - Faster training with multiple UE5 instances
echo    - Requires N UE5 instances running on sequential ports
echo.
echo ================================================================================
echo.
set /p MODE="Enter choice (1 or 2): "

if "%MODE%"=="1" goto :single_mode
if "%MODE%"=="2" goto :multi_mode
echo Invalid choice. Exiting.
pause
exit /b 1

:single_mode
echo.
set /p ITERATIONS="Enter number of training iterations (default: 100): "
if "%ITERATIONS%"=="" set ITERATIONS=100

echo.
echo ================================================================================
echo Starting Single-Instance Training
echo ================================================================================
echo Iterations: %ITERATIONS%
echo Workers: 0 (single process)
echo UE5 Port: 50051
echo ================================================================================
echo.
timeout /t 3 /nobreak 1>nul

docker-compose run --rm -e NUM_ITERATIONS=%ITERATIONS% training-single python train_rllib.py --iterations %ITERATIONS% --host host.docker.internal --port 50051
goto :training_complete

:multi_mode
echo.
set /p NUM_WORKERS="Enter number of workers (e.g., 2, 4, 8): "
if "%NUM_WORKERS%"=="" (
    echo ERROR: Number of workers is required for multi-instance training
    pause
    exit /b 1
)

echo.
set /p ITERATIONS="Enter number of training iterations (default: 100): "
if "%ITERATIONS%"=="" set ITERATIONS=100

echo.
echo ================================================================================
echo Starting Multi-Instance Training
echo ================================================================================
echo Iterations: %ITERATIONS%
echo Workers: %NUM_WORKERS%
echo UE5 Ports: 50051-%END_PORT% (make sure %NUM_WORKERS% UE5 instances are running!)
echo ================================================================================
echo.
set /a MAX_WORKER=%NUM_WORKERS%-1
echo IMPORTANT: You must have %NUM_WORKERS% UE5 instances running on ports:
for /L %%i in (0,1,%MAX_WORKER%) do (
    set /a PORT=50051+%%i
    echo   - Port !PORT!
)
echo.
set /p CONFIRM="Press ENTER to continue, or CTRL+C to cancel... "

timeout /t 3 /nobreak 1>nul

docker-compose run --rm -e NUM_WORKERS=%NUM_WORKERS% -e NUM_ITERATIONS=%ITERATIONS% training-multi python train_rllib.py --iterations %ITERATIONS% --host host.docker.internal --port 50051
goto :training_complete

:training_complete
echo.
echo ================================================================================
echo Training complete!
echo ================================================================================
pause
