@echo off
setlocal

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

REM [4/4] Start training
echo [4/4] Starting training...
timeout /t 3 /nobreak 1>nul

if "%~1"=="" (
    docker-compose run --rm training-single
) else (
    docker-compose run --rm training-single python train_rllib.py --iterations %~1
)

echo.
echo ================================================================================
echo Training complete!
echo ================================================================================
pause
