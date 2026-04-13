@echo off
chcp 65001 >nul
cd /d E:\soft\v2rayN-windows-64\bin\xray
start "" xray.exe run -c "E:\eclipse_workspace\multiple_thread_validproxy\bin\main_config_10085.json"
timeout /t 3 /nobreak >nul
echo Xray started
