#!/usr/bin/env pwsh
# Quick launch script for HiMYM Scene Editor

Write-Host "Starting HiMYM Scene Editor..." -ForegroundColor Cyan

# Check if editor exists
if (!(Test-Path "build\bin\Release\editor_app.exe")) {
    Write-Host "Editor not found! Building..." -ForegroundColor Yellow
    cmake --build build --config Release --target editor_app
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }
}

# Launch editor
.\build\bin\Release\editor_app.exe

Write-Host "Editor closed." -ForegroundColor Green
