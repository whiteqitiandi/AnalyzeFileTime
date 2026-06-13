@echo off
setlocal enableextensions

REM === (1) 設定你的 Qt 根目錄 (請改成你的實際路徑) ===
REM 範例: G:\Qt\5.15.11_x64\bin\windeployqt.exe
set "QTDIR=C:\Qt\Qt5.12.3\5.12.3\msvc2017_64"
set "WINDEPLOY=%QTDIR%\bin\windeployqt.exe"

REM === (2) 檢查 windeployqt 是否存在 ===
if not exist "%WINDEPLOY%" (
    echo [錯誤] 找不到 windeployqt: "%WINDEPLOY%"
    echo 請確認 QTDIR 設定正確，並且該目錄下有 bin\windeployqt.exe
    pause
    exit /b 1
)

REM === (3) 取得最新的 EXE (避免多個 EXE 時抓到錯的) ===
set "EXE="
for /f "usebackq delims=" %%f in (`dir /b /a:-d /o:-d *.exe`) do (
    set "EXE=%%f"
    goto :found_exe
)

:found_exe
if "%EXE%"=="" (
    echo [錯誤] 這個資料夾沒有找到任何 .exe
    pause
    exit /b 1
)
echo [資訊] 目標執行檔: %EXE%

REM === (4) 顯示 windeployqt 版本 & 路徑，方便確認 ===
echo.
echo [資訊] windeployqt 路徑: "%WINDEPLOY%"
"%WINDEPLOY%" --version
echo.

REM === (5) 清理 AnalyzeFileTime/ 並建立 ===
if exist AnalyzeFileTime (
    echo [資訊] 移除舊的 AnalyzeFileTime 資料夾...
    rmdir /s /q AnalyzeFileTime
)
mkdir AnalyzeFileTime

REM === (6) 先複製 EXE 到 AnalyzeFileTime/ ===
copy /y "%EXE%" "AnalyzeFileTime\" >nul
if errorlevel 1 (
    echo [錯誤] 複製 EXE 失敗
    pause
    exit /b 1
)

REM === (7) 執行 windeployqt 部署 DLL/Plugins ===
echo [資訊] 開始部署 DLL/Plugins...
REM 提示：
REM  - --release：針對 Release
REM  - --compiler-runtime：連同 MSVC 執行階段 DLL 一起帶上
REM  - --verbose 2：輸出較多細節，有助除錯
REM 若你的應用有用到 QML，請加上 --qmldir 路徑
"%WINDEPLOY%" --release --compiler-runtime --verbose 2 "AnalyzeFileTime\%EXE%"
if errorlevel 1 (
    echo [錯誤] windeployqt 執行失敗，請檢查上方訊息
    pause
    exit /b 1
)

echo.
echo [完成] 可攜式版本已生成於 AnalyzeFileTime\ 資料夾
echo        請把整個 AnalyzeFileTime\ 夾給使用者即可。
echo.
pause
