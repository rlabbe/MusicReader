pyinstaller --onefile set_bookmarks.py

REM @echo off
setlocal

robocopy dist bin set_bookmarks.exe /XO /NFL /NDL /NJH /NJS >nul
