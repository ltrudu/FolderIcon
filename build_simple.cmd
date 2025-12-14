@echo off
echo Building FolderIcon (C version)...
cl /nologo /O2 /GL /GS- /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN main.c /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS user32.lib shell32.lib gdi32.lib comctl32.lib dwmapi.lib uxtheme.lib ole32.lib /OUT:FolderIcon.exe
if %ERRORLEVEL% EQU 0 (
    echo Build successful: FolderIcon.exe
    del *.obj 2>nul
) else (
    echo Build failed!
)
