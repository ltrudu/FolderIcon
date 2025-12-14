@echo off
setlocal

:: Find Visual Studio
for %%v in (18 17 16 2026 2025 2024 2022 2019) do (
    for %%e in (Enterprise Professional Community Preview BuildTools) do (
        if exist "C:\Program Files\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat" (
            call "C:\Program Files\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
            goto :build
        )
    )
)

echo Visual Studio not found!
exit /b 1

:build
echo Building FolderIcon (C version)...

:: Compile with maximum optimization
cl /nologo /O2 /GL /GS- /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   main.c ^
   /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
   user32.lib shell32.lib gdi32.lib comctl32.lib dwmapi.lib uxtheme.lib ole32.lib ^
   /OUT:FolderIcon.exe

if %ERRORLEVEL% EQU 0 (
    echo Build successful: FolderIcon.exe
    del *.obj 2>nul
) else (
    echo Build failed!
)
