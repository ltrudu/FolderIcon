# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FolderIcon is a Windows desktop utility written in pure C (C17) that displays folder contents in a popup window with icons. It features:
- Dark/light mode detection from Windows theme settings
- Windows 11 rounded corners and DWM integration
- ListView with icons, tooltips, and click-to-open functionality
- Fade-out animation on close
- Smart window positioning relative to cursor and taskbar

## Build Commands

### Visual Studio (Recommended)
```cmd
# Open in Visual Studio
FolderIcon.sln

# Build from command line with MSBuild
msbuild FolderIcon.sln /p:Configuration=Release /p:Platform=x64
msbuild FolderIcon.sln /p:Configuration=Debug /p:Platform=x64
```

### Command Line (requires VS Developer Command Prompt)
```cmd
# Full build script (auto-detects VS installation)
build.bat

# Simple build (requires vcvars64.bat already sourced)
build_simple.cmd
```

### CMake
```cmd
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Output Locations

| Configuration | Output Path |
|--------------|-------------|
| Debug | `bin\Debug\FolderIcon.exe` |
| Release | `bin\Release\FolderIcon.exe` |
| build.bat | `FolderIcon.exe` (project root) |

## Architecture

**Single-file implementation** (`main.c` ~550 lines):
- No external dependencies beyond Windows SDK
- All functionality contained in one C file
- Uses Win32 API directly (no MFC/ATL/WTL)

**Key Components:**
- `FolderEntry` struct: Holds file/folder metadata and icon index
- `g_*` globals: Application state (folder path, items, colors, window handles)
- `WndProc`: Main window message handler
- `ListViewSubclassProc`: Custom ListView behavior for hover/click

**Windows APIs Used:**
- Shell API (`SHGetFileInfoW`) for icons
- Common Controls (`ListView`, `Tooltip`)
- DWM API for Windows 11 theming
- Registry API for dark mode detection

## Usage

```cmd
FolderIcon.exe [--folder|-f] <path>
FolderIcon.exe <path>
FolderIcon.exe              # Opens Desktop folder
```

## Platform Requirements

- Windows 10/11 (x64)
- Visual Studio 2019+ with "Desktop development with C++" workload
- Windows SDK 10.0+
