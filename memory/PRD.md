# Protoon - Product Requirements Document

## Original Problem Statement
Build "Protoon" - a Roblox asset & map extraction tool combining Fleasion (HTTP proxy asset extractor) and UniversalSynSaveInstance (LUA map saving script) into a standalone Windows .exe. The website serves as a download portal. The C++ tool uses kernel-level memory reading to extract game data and Roblox CDN for asset downloads.

## Architecture
```
/app/
├── .github/workflows/build.yml     # GitHub Actions: builds C++ on Windows, creates releases
├── backend/
│   ├── protoon_kernel/
│   │   ├── main.cpp                # C++ user-mode app (interactive menu, asset download, RBXLX export)
│   │   ├── memory_reader.hpp       # Memory reading library (DataModel traversal, asset refs)
│   │   ├── driver.c                # Kernel driver for undetected mode
│   │   ├── FULL_GUIDE.md           # User documentation
│   │   └── BUILD.md                # Driver build instructions
│   ├── server.py                   # FastAPI backend (tool info, download URLs)
│   └── .env                        # MONGO_URL, DB_NAME
├── frontend/
│   ├── src/App.js                  # Main React app (hero, features, tools, how-it-works)
│   └── .env                        # REACT_APP_BACKEND_URL
└── memory/PRD.md
```

## User Personas
- **Roblox Game Developers**: Want to extract maps/assets from games for reference or learning
- **Content Creators**: Need game assets for thumbnails, videos, recreations
- **Reverse Engineers**: Studying Roblox internals and game design

## Core Requirements (Static)
1. Windows .exe that extracts Roblox game data without requiring an executor
2. Fleasion-style extraction options (decals, audio, animations, meshes, sky)
3. Full map saving to .rbxlx format
4. Organized Downloads folder per game with categorized subfolders
5. Website with download portal and documentation
6. GitHub Actions CI/CD for automated builds and releases

## What's Been Implemented

### v1.0.0 - v1.0.1 (Initial)
- C++ kernel driver + user-mode scaffold
- Basic DataModel finding and Workspace extraction
- Website with dark gaming theme and download modal
- GitHub Actions build workflow
- Bug fix: console window staying open

### v1.1.0 (2026-03-15) - Current
- **Interactive extraction menu** with 8 Fleasion-style options
- **Asset downloading** from Roblox CDN via WinHTTP
- **Organized Downloads folder**: `Downloads/Game_PlaceID/{Decals,Audio,Animations,Meshes,Sky}/`
- **Enhanced memory_reader.hpp**: Debug logging, multiple traversal methods (DataModel children + Workspace), batch child reading, asset reference collection
- **Better RBXLX generation**: Full CFrame rotation, color, material
- **Debug mode**: `--debug` flag for verbose diagnostics
- **Asset manifest**: Text file listing all discovered assets
- **Updated website**: New feature descriptions, extraction options in download modal, updated How It Works

## Prioritized Backlog

### P0 (Critical)
- **User must test v1.1.0**: The core memory traversal needs validation. Current issue: previous version only found 1 instance. New version has multiple traversal methods and debug logging.
- **Make GitHub repo public**: Download links fail for public users while repo is private.

### P1 (Important)
- **Validate asset downloading**: Test that WinHTTP downloads work on user's Windows machine
- **Improve DataModel reconstruction**: If traversal still gets few instances, investigate offsets or add memory scanning
- **Add more property reading**: Scripts, LocalScripts, Humanoid properties

### P2 (Nice to Have)
- **Kernel driver signing**: Currently requires test signing mode. Production needs proper code signing certificate.
- **Auto-offset updates**: Fetch latest offsets from NtReadVirtualMemory repo on startup
- **Progress bar**: Show download progress percentage for assets
- **Game name resolution**: Use Roblox API to get game name from PlaceId for folder naming

## Next Tasks
1. User tests v1.1.0 with `--debug` flag and reports console output
2. Based on output, fix any remaining traversal issues
3. User makes GitHub repo public
4. Implement deeper DataModel parsing if needed (scripts, GUI elements, etc.)

## 3rd Party Integrations
- **GitHub API**: Push code, create tags/releases via PAT
- **Roblox CDN**: `assetdelivery.roblox.com` for downloading assets by ID
- **NtReadVirtualMemory/Roblox-Offsets-Website**: Source for current Roblox memory offsets

## Credentials
- GitHub PAT: `github_pat_11B5VECTI0...` (stored in git remote)

## Testing Status
- Website: All tests passed (100% backend + frontend)
- C++ Tool: Cannot test in Linux environment - requires user testing on Windows
