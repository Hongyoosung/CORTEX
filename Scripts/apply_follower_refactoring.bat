@echo off
REM ============================================================================
REM CORTEX v8.0: Apply FollowerAgentComponent Refactoring
REM ============================================================================
REM This script backs up the original FollowerAgentComponent files and replaces
REM them with the refactored versions that delegate to sub-components.
REM
REM IMPORTANT: Run this from the project root directory!
REM ============================================================================

echo.
echo ========================================
echo CORTEX v8.0 Refactoring Tool
echo ========================================
echo.
echo This will:
echo   1. Backup original FollowerAgentComponent files
echo   2. Replace with refactored versions
echo   3. Keep new component files (TacticalState, ObservationBuilder, etc.)
echo.

set /p CONFIRM="Continue? (Y/N): "
if /i not "%CONFIRM%"=="Y" (
    echo Cancelled.
    exit /b 0
)

echo.
echo [1/4] Creating backup directory...
set BACKUP_DIR=Backup\FollowerAgent_Original_%date:~-4,4%%date:~-10,2%%date:~-7,2%_%time:~0,2%%time:~3,2%%time:~6,2%
set BACKUP_DIR=%BACKUP_DIR: =0%
mkdir "%BACKUP_DIR%" 2>nul

echo [2/4] Backing up original files...
copy "Source\GameAI_Project\Public\Team\FollowerAgentComponent.h" "%BACKUP_DIR%\FollowerAgentComponent.h.bak"
copy "Source\GameAI_Project\Private\Team\FollowerAgentComponent.cpp" "%BACKUP_DIR%\FollowerAgentComponent.cpp.bak"

echo [3/4] Replacing with refactored versions...
copy /Y "Source\GameAI_Project\Public\Team\FollowerAgentComponent_Refactored.h" "Source\GameAI_Project\Public\Team\FollowerAgentComponent.h"
copy /Y "Source\GameAI_Project\Private\Team\FollowerAgentComponent_Refactored.cpp" "Source\GameAI_Project\Private\Team\FollowerAgentComponent.cpp"

echo [4/4] Cleaning up temporary files...
del "Source\GameAI_Project\Public\Team\FollowerAgentComponent_Refactored.h"
del "Source\GameAI_Project\Private\Team\FollowerAgentComponent_Refactored.cpp"

echo.
echo ========================================
echo Refactoring Applied Successfully!
echo ========================================
echo.
echo Backup saved to: %BACKUP_DIR%
echo.
echo NEXT STEPS:
echo   1. Rebuild your project in Visual Studio or Rider
echo   2. Fix any compilation errors (should be minimal)
echo   3. Update any existing Blueprints to add new components:
echo      - TacticalStateComponent
echo      - ObservationBuilderComponent
echo      - RLAgentComponent
echo      - CombatExecutorComponent
echo   4. Run tests to verify backwards compatibility
echo.
echo See Docs\Refactoring_FollowerAgentComponent_v8.0.md for details.
echo.

pause
