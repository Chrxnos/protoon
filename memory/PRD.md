# Protoon - Roblox Asset & Map Extraction Suite

## Original Problem Statement
Build a website called "Protoon" that combines:
1. **Fleasion** - HTTP proxy-based asset extraction tool
2. **UniversalSynSaveInstance (USSI)** - LUA script for saving entire game maps

User requested UDP-based map saving converted to .exe like Fleasion, with a dark gaming aesthetic website.

## Architecture

### Backend (FastAPI)
- `/app/backend/server.py` - Main API server
- `/app/backend/protoon/` - Core Protoon package
  - `raknet.py` - RakNet protocol parser (based on roblox-dissector)
  - `datamodel.py` - Roblox Instance hierarchy & RBXLX export
  - `packet_capture.py` - UDP packet capture & DataModel reconstruction
- `protoon_gui.py` - PyQt6 desktop GUI (for Windows .exe build)
- `Protoon.spec` - PyInstaller build spec

### Frontend (React)
- Dark gaming aesthetic landing page
- Tool downloads section (Protoon, Fleasion, USSI)
- Demo data loading & RBXLX export functionality
- USSI code snippet section

### API Endpoints
- `GET /api/` - API info
- `GET /api/tools` - List downloadable tools
- `POST /api/capture/demo` - Load demo data
- `GET /api/capture/instances` - Get captured instances
- `POST /api/capture/export` - Export to RBXLX
- `GET /api/download/ussi` - Download USSI script

## What's Been Implemented ✅

### Jan 2026
1. **RakNet Protocol Parser**
   - BitstreamReader for binary packet parsing
   - RakNet layer decoding (flags, ACKs, datagram numbers)
   - Reliable packet handling with split packet reassembly
   - Property type definitions matching Roblox schema

2. **DataModel System**
   - Instance class with properties, children, parent relationships
   - DataModel container with services management
   - RBXLX XML export functionality
   - Reference pool for serialization

3. **Packet Capture Module**
   - UDP socket-based capture (requires admin on Windows)
   - RakNet packet processing pipeline
   - Instance creation from ID_REPLIC_NEW_INSTANCE packets
   - Schema sync handling
   - MockPacketCapture for demo/testing

4. **Landing Page Website**
   - Dark gaming aesthetic with Orbitron/Rajdhani fonts
   - Animated hero section with "PROTOON" branding
   - Download Tools section with 3 tools
   - Features grid showcasing capabilities
   - Try It Now demo section
   - USSI quick start code block
   - How It Works steps
   - Footer with resource links

5. **PyQt6 Desktop GUI** (Windows .exe)
   - Capture/Stop/Export buttons
   - Real-time statistics display
   - Instance browser
   - Log viewer
   - Dark theme matching website

## Technical Details

### UDP Packet Interception Approach
As discussed with user - the approach intercepts Roblox UDP packets BEFORE they hit the client:
- Part positions and scripts sent via UDP (stored in RAM)
- No Hyperion bypass required (intercepting before client)
- Based on RakNet protocol analysis from roblox-dissector

### Limitations
- Protocol may change with Roblox updates
- Full map capture requires walking around to trigger replication
- Some instance types may not replicate via network

## Prioritized Backlog

### P0 - Done ✅
- [x] Landing page with downloads
- [x] Demo data & export functionality
- [x] RakNet protocol parser
- [x] DataModel & RBXLX export

### P1 - Future Development
- [ ] Update protocol parser for current Roblox version
- [ ] Test with live Roblox traffic
- [ ] Add Zstd decompression for join data
- [ ] Build and distribute Windows .exe

### P2 - Nice to Have
- [ ] Asset preview in web interface
- [ ] Script decompilation
- [ ] Real-time instance visualization
- [ ] Discord bot integration

## User Personas
1. **Roblox Developers** - Extract assets for learning/analysis
2. **Map Archivists** - Save game worlds for preservation
3. **Modders** - Extract and modify game content

## Core Requirements (Static)
- Dark gaming aesthetic ✅
- Download button visible ✅
- Combines Fleasion + USSI functionality ✅
- Shows "Protoon" branding ✅
- Open source friendly ✅

## Next Tasks
1. Test with actual Roblox UDP traffic
2. Build Windows .exe with PyInstaller
3. Host .exe for download
4. Update protocol for latest Roblox version
