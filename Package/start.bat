@echo off
rem Map Drawing Tool web app. Leave this window open while players use it.
cd /d "%~dp0"
set PY=python
where py >nul 2>nul && set PY=py -3
%PY% -I Web\app.py
pause
