@echo off
REM Docker-based SBDAPM RLlib Training Runner
REM Solves Windows multiprocessing (spawn) DLL issues

echo ============================================================
echo SBDAPM Docker Training
echo ============================================================
echo.

REM Check if Docker is available
where docker >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Docker not found in PATH
    echo Please ensure Docker Desktop is installed and running
    pause
    exit /b 1
)

REM Check Docker daemon
docker info >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Docker daemon not running
    echo Please start Docker Desktop
    pause
    exit /b 1
)

echo Select training mode:
echo   1. Single-worker (NUM_WORKERS=0, local only)
echo   2. Multi-worker (NUM_WORKERS=4, requires 4 UE instances)
echo   3. TensorBoard only (monitoring)
echo   4. Test connection to UE5 (diagnostics)
echo.

set /p MODE="Enter choice (1-4): "

if "%MODE%"=="1" (
    echo Starting single-worker training...
    docker-compose --profile single up --build
) else if "%MODE%"=="2" (
    echo Starting multi-worker training...
    echo NOTE: Ensure 4 UE instances are running on ports 50051-50054
    docker-compose --profile multi up --build
) else if "%MODE%"=="3" (
    echo Starting TensorBoard...
    echo Open http://localhost:6006 in your browser
    docker-compose --profile monitor up --build
) else if "%MODE%"=="4" (
    echo Testing connection to UE5...
    echo Make sure UE5 is running first!
    docker-compose --profile test up --build
) else (
    echo Invalid choice
    pause
    exit /b 1
)

echo.
echo Training completed!
pause
