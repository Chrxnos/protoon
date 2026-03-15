# Protoon - Product Requirements Document

## Original Problem Statement
Build "Protoon" - a Roblox asset & map extraction tool combining Fleasion (HTTP proxy asset extractor) and UniversalSynSaveInstance (LUA map saving script) into a standalone Windows .exe. The website serves as a download portal. The C++ tool uses kernel-level memory reading to extract game data and Roblox CDN for asset downloads.

## Architecture
```
/app/
├── .github/workflows/build.yml     # GitHub Actions: builds C++ on Windows, creates releases
├── backend/
│   ├── protoon_kernel/
│   │   ├── main.cpp                # C++ user-mode app (interactive menu, auth CDN download, RBXLX export)
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
7. Authenticated CDN downloads via .ROBLOSECURITY cookie

## What's Been Implemented

### v1.0.0 - v1.0.1 (Initial)
- C++ kernel driver + user-mode scaffold
- Basic DataModel finding and Workspace extraction
- Website with dark gaming theme and download modal
- GitHub Actions build workflow
- Bug fix: console window staying open

### v1.1.0 (2026-03-15)
- **Interactive extraction menu** with 8 Fleasion-style options
- **Asset downloading** from Roblox CDN via WinHTTP
- **Organized Downloads folder**: `Downloads/Game_PlaceID/{Decals,Audio,Animations,Meshes,Sky}/`
- **Enhanced memory_reader.hpp**: Debug logging, multiple traversal methods
- **Better RBXLX generation**: Full CFrame rotation, color, material
- **Debug mode**: `--debug` flag for verbose diagnostics
- **Asset manifest**: Text file listing all discovered assets

### v1.3.0 (2026-03-16)
- **Fixed children traversal**: Confirmed Method B (ptr-to-vector) working
- **Major breakthrough**: Successfully extracting 75k+ instances from Roblox
- **Fixed CFrame reading**: Column-major to row-major conversion
- **Fixed Workspace filtering**: Only visual parts in RBXLX export

### v1.4.1 (2026-01-28) — Current
- **Fixed path duplication (for real)**: Added deduplication safety net that detects `Downloads/Game_XXX/Downloads/Game_XXX` and trims it. Uses `GetModuleFileNameW` (wide API) + prints exe directory for diagnostics. Removed `fs::absolute()` calls that resolved from CWD.
- **Fixed Roblox Studio Terrain error**: Added `Terrain` to skipClasses in RBXLX generator. Workspace already has a built-in Terrain instance; adding another causes "Unable to change Terrain's parent" error.
- **Fixed map-only mode**: Choice [1] no longer collects, displays, or saves asset references. No asset manifest generated. Clean map-only extraction.

### v1.4.0 (2026-01-28)
- **Authenticated CDN downloads**: Supports `.ROBLOSECURITY` cookie (required since April 2025)
- **New OpenCloud API endpoint**: Uses `apis.roblox.com/asset-delivery-api` + legacy fallback
- **Retry logic**: Automatic retry on failed downloads with 500ms delay
- **CDN redirect handling**: Follows JSON location responses to actual CDN URLs
- **Better file detection**: Recognizes GIF, WebP, JSON formats
- **Cookie support**: `cookie.txt` file or `--cookie` command-line flag
- **Path duplication fix (initial)**: Uses exe directory as base instead of CWD
- **Updated website**: v1.4.x across all frontend, backend, and build references

## Prioritized Backlog

### P0 (Critical) — DONE
- ~~Fix path duplication bug~~ DONE in v1.4.0
- ~~Update version to v1.4.0~~ DONE
- ~~Push to GitHub & create release~~ DONE
- **Make GitHub repo public**: Download links fail for public visitors while repo is private

### P1 (Important)
- **Validate authenticated downloads**: Test that cookie-based CDN downloads work on user's Windows machine
- **Auto-cookie extraction**: Read .ROBLOSECURITY from browser cookies or registry automatically
- **Improve DataModel reconstruction**: If traversal still gets few instances, investigate offsets

### P2 (Nice to Have)
- **Kernel driver signing**: Currently requires test signing mode. Production needs proper code signing certificate
- **Auto-offset updates**: Fetch latest offsets from NtReadVirtualMemory repo on startup
- **Progress bar**: Show download progress percentage for assets
- **Game name resolution**: Use Roblox API to get game name from PlaceId for folder naming
- **Advanced asset categorization**: AI/rule-based sorting of assets (VFX, faces, props, etc.)

## Next Tasks
1. User tests v1.4.0 — especially authenticated CDN downloads with cookie.txt
2. Make GitHub repo public so download links work for visitors
3. Consider auto-cookie extraction from browser
4. Based on feedback, fix any remaining issues

## 3rd Party Integrations
- **GitHub API**: Push code, create tags/releases via PAT
- **Roblox CDN**: `apis.roblox.com/asset-delivery-api` + `assetdelivery.roblox.com` for downloading assets
- **NtReadVirtualMemory/Roblox-Offsets-Website**: Source for current Roblox memory offsets

## Credentials
- GitHub PAT: `github_pat_11B5VECTI0...` (stored in git remote)

## Testing Status
- Website: All tests passed (100% backend + frontend) — iteration 4
- C++ Tool: Cannot test in Linux environment — requires user testing on Windows
