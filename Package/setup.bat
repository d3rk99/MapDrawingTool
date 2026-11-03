@echo off
rem Map Drawing Tool web app: installs the Python packages, makes the map images, writes Web\config.json.
cd /d "%~dp0"
set PY=python
where py >nul 2>nul && set PY=py -3
%PY% -m pip install --disable-pip-version-check -r Web\requirements.txt || goto fail
%PY% -I Web\setup.py || goto fail
pause
exit /b 0
:fail
echo.
echo Setup failed. Python 3.10 or newer from python.org is needed (tick "Add python.exe to PATH").
pause
exit /b 1
