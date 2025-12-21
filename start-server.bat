@echo off
title Tibia Game Server 7.7 - Zanera

echo ============================================
echo   Tibia Game Server 7.7 - Windows Edition
echo   (c) CIP Productions, 2003
echo ============================================
echo.

:: Change to the game directory
cd /d "%~dp0"

:: Remove stale PID file if exists
if exist "save\game.pid" (
    echo Removing stale PID file...
    del /q "save\game.pid"
)

:: Check if QueryManager is running
echo Checking QueryManager connection...
powershell -Command "Test-NetConnection -ComputerName 127.0.0.1 -Port 7173 -WarningAction SilentlyContinue | Select-Object -ExpandProperty TcpTestSucceeded" > nul 2>&1
if errorlevel 1 (
    echo WARNING: QueryManager might not be running on port 7173
    echo Make sure to start the QueryManager first!
    echo.
)

:: Start the game server
echo Starting Tibia Game Server...
echo.
build\bin\tibia-game.exe %*

:: If we get here, the server has stopped
echo.
echo Server stopped.
if exist "save\game.pid" (
    del /q "save\game.pid"
)
pause
