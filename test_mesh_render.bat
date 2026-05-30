@echo off
echo Testing mesh_demo rendering...
cd /d "E:\code\cpp\mono\himym"
.\build\bin\Release\mesh_demo.exe
echo.
echo If you saw a window with a black screen, the window opened but rendering failed.
echo Check if any error dialog appeared.
pause
