@echo off
setlocal enabledelayedexpansion

REM 배치파일이 있는 현재 위치로 이동
cd /d "%~dp0"

echo ================================================================================
echo CORTEX v8.0 Docker Training Launcher
echo ================================================================================
echo.

REM [1/4] Docker 체크
docker info >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Docker is not running!
    echo Please start Docker Desktop and try again.
    pause
    exit /b 1
)
echo [1/4] Docker is running
echo.

REM [2/4] 이미지 체크 및 빌드
docker image inspect cortex_training-training-single >nul 2>&1
if %errorlevel% neq 0 (
    echo [2/4] Building Docker image (first time only)...
    docker-compose build training-single
    if %errorlevel% neq 0 (
        echo ERROR: Docker build failed!
        pause
        exit /b 1
    )
) else (
    echo [2/4] Docker image already built (skipping build)
)
echo.

REM [3/4] 연결 테스트
echo [3/4] Testing connection to UE5...
docker-compose run --rm test-connection
if %errorlevel% neq 0 (
    echo ERROR: Cannot connect to UE5!
    echo 1. UE5 is running? 2. Press Play? 3. Check Port 50051?
    pause
    exit /b 1
)
echo.

REM [4/4] 학습 시작
echo [4/4] Starting training...
timeout /t 3 /nobreak >nul

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