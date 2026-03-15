# Protoon v1.4.0 - Complete Build & Usage Guide

## Prerequisites (One-Time Setup)

### 1. Install Required Software

```powershell
# Install Visual Studio 2022 (Community is free)
# Download from: https://visualstudio.microsoft.com/
# During install, select:
#   - "Desktop development with C++"
#   - "Windows 10/11 SDK"

# Install Windows Driver Kit (WDK)
# Download from: https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk
# Install AFTER Visual Studio
```

### 2. Enable Test Signing (Run as Admin)

```powershell
# Open PowerShell as Administrator
bcdedit /set testsigning on

# Reboot your computer
shutdown /r /t 0
```

After reboot, you'll see "Test Mode" watermark on desktop - this is normal.

---

## Building Protoon

### Step 1: Build the Kernel Driver

**Option A: Using Visual Studio (Recommended)**

1. Open Visual Studio 2022
2. File → New → Project
3. Search for "Kernel Mode Driver, Empty (KMDF)"
4. Name it "ProtoonDriver", click Create
5. In Solution Explorer, right-click "Source Files" → Add → Existing Item
6. Navigate to `/app/backend/protoon_kernel/` and select `driver.c`
7. Build → Build Solution (Ctrl+Shift+B)
8. Output: `x64\Release\ProtoonDriver.sys`

**Option B: Using Command Line**

```powershell
# Open "Developer Command Prompt for VS 2022" as Admin
cd C:\path\to\protoon_kernel

# Create build directory
mkdir build_driver
cd build_driver

# Compile (adjust paths for your WDK version)
cl /c /Zp8 /Gy /W4 /WX /Gz /Zi ^
   /D "UNICODE" /D "_UNICODE" /D "KMDF_VERSION_MAJOR=1" ^
   /D "_AMD64_" /D "AMD64" ^
   /I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\km" ^
   ..\driver.c

# Link
link /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry ^
     /OUT:ProtoonDriver.sys driver.obj ^
     "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\km\x64\ntoskrnl.lib"
```

### Step 2: Build the User-Mode Application

```powershell
# Open "Developer Command Prompt for VS 2022"
cd C:\path\to\protoon_kernel

# Simple compile
cl /EHsc /std:c++17 /O2 /Fe:Protoon.exe main.cpp

# Or with CMake
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

Output: `Protoon.exe`

---

## Installing the Driver

### Step 3: Install Kernel Driver (Run as Admin)

```powershell
# Open PowerShell as Administrator

# Copy driver to a permanent location
mkdir C:\Protoon
copy ProtoonDriver.sys C:\Protoon\

# Create the driver service
sc create ProtoonDrv type= kernel binpath= "C:\Protoon\ProtoonDriver.sys"

# Start the driver
sc start ProtoonDrv

# Verify it's running
sc query ProtoonDrv
# Should show: STATE: 4  RUNNING
```

---

## Using Protoon to Extract Maps

### Step 4: Extract a Roblox Map

```powershell
# 1. Launch Roblox and join the game you want to extract

# 2. Once in-game, open PowerShell as Administrator
cd C:\Protoon

# 3. Run Protoon
.\Protoon.exe

# Output will show:
# [*] Initializing Protoon...
# [Protoon] Kernel driver connected
# [Protoon] Found Roblox PID: 12345
# [Protoon] Roblox base: 0x7FF6ABCD0000
# [Protoon] DataModel: 0x1234567890
# [*] Connected to Roblox (PID: 12345)
# [*] Extracting map data...
# [*] Extracted 1523 instances
# [*] Found 847 parts
# [+] Map saved to: extracted_map.rbxlx

# 4. Custom output filename
.\Protoon.exe my_game_map.rbxlx
```

### Step 5: Open in Roblox Studio

1. Open Roblox Studio
2. File → Open from File
3. Select `extracted_map.rbxlx`
4. The map will load with all parts, positions, and properties!

---

## Complete Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    ONE-TIME SETUP                                │
├─────────────────────────────────────────────────────────────────┤
│ 1. Install Visual Studio 2022 + WDK                             │
│ 2. bcdedit /set testsigning on                                  │
│ 3. Reboot                                                        │
│ 4. Build driver.c → ProtoonDriver.sys                           │
│ 5. Build main.cpp → Protoon.exe                                 │
│ 6. sc create ProtoonDrv ... && sc start ProtoonDrv              │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    EACH TIME YOU EXTRACT                         │
├─────────────────────────────────────────────────────────────────┤
│ 1. Launch Roblox                                                 │
│ 2. Join the game you want to extract                            │
│ 3. Run Protoon.exe (as Admin)                                   │
│ 4. Wait for extraction (few seconds)                            │
│ 5. Open .rbxlx in Roblox Studio                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Troubleshooting

### "Kernel driver not loaded"
```powershell
# Check driver status
sc query ProtoonDrv

# If not running, start it
sc start ProtoonDrv

# If it fails, check Windows Event Viewer:
# Event Viewer → Windows Logs → System
# Look for errors from "Service Control Manager"
```

### "Roblox process not found"
- Make sure you're in a game, not just the Roblox launcher
- The process should be `RobloxPlayerBeta.exe`

### "Could not find DataModel"
Offsets may have changed. Update them:
1. Go to https://robloxoffsets.pages.dev/
2. Copy new offset values
3. Update `memory_reader.hpp` in `namespace RobloxOffsets`
4. Rebuild Protoon.exe

### "Access denied"
- Run PowerShell/Command Prompt as Administrator
- Make sure test signing is enabled

### Driver won't load
```powershell
# Disable driver signature enforcement temporarily:
# 1. Hold Shift + Click Restart
# 2. Troubleshoot → Advanced → Startup Settings → Restart
# 3. Press 7 to disable driver signature enforcement
# 4. Try loading driver again
```

---

## Stopping/Removing the Driver

```powershell
# Stop the driver
sc stop ProtoonDrv

# Remove the driver service
sc delete ProtoonDrv

# (Optional) Disable test signing when done
bcdedit /set testsigning off
# Reboot
```

---

## What Gets Extracted

| Property | Extracted |
|----------|-----------|
| Part Position (X,Y,Z) | Yes |
| Part Rotation (CFrame) | Yes |
| Part Size | Yes |
| Part Anchored | Yes |
| Part CanCollide | Yes |
| Part Transparency | Yes |
| Part Material | Yes |
| Part Color (MeshPart) | Yes |
| Models & Hierarchy | Yes |
| Instance Names | Yes |
| Decals / Images | Yes (downloaded from CDN) |
| Audio / Sounds | Yes (downloaded from CDN) |
| Animations | Yes (downloaded from CDN) |
| Meshes | Yes (downloaded from CDN) |
| Sky Textures | Yes (downloaded from CDN) |
| Scripts | No (bytecode only) |

**Note:** CDN downloads require authentication. See "Authentication" section below.

---

## Authentication for Asset Downloads

Since April 2025, Roblox requires authentication for asset downloads. Two options:

### Option 1: cookie.txt (Recommended)
1. Log into Roblox in your browser
2. Open browser DevTools (F12) → Application → Cookies
3. Find `.ROBLOSECURITY` cookie and copy its value
4. Create a file called `cookie.txt` next to `Protoon.exe`
5. Paste the cookie value into it (just the value, no quotes)

### Option 2: Command-line flag
```powershell
.\Protoon.exe --cookie YOUR_ROBLOSECURITY_COOKIE_VALUE_HERE
```

Without authentication, map extraction and instance tree still work perfectly — only CDN asset downloads will fail.

---

## Quick Reference Commands

```powershell
# === SETUP ===
bcdedit /set testsigning on          # Enable test signing
sc create ProtoonDrv type= kernel binpath= "C:\Protoon\ProtoonDriver.sys"
sc start ProtoonDrv                   # Start driver

# === USAGE ===
.\Protoon.exe                         # Interactive menu (8 options)
.\Protoon.exe --debug                 # With verbose diagnostics
.\Protoon.exe --output D:\MyExports   # Custom output directory
.\Protoon.exe --cookie COOKIE_VALUE   # With auth for downloads

# === CLEANUP ===
sc stop ProtoonDrv                    # Stop driver
sc delete ProtoonDrv                  # Remove driver
bcdedit /set testsigning off          # Disable test signing
```

---

## Safety Notes

1. **Test Signing Mode** - Your Windows will show "Test Mode" watermark. This is normal and required for unsigned drivers.

2. **Anti-Virus** - Some AV software may flag kernel drivers. You may need to add an exception.

3. **Roblox Updates** - When Roblox updates, offsets may change. Check the offsets website and rebuild if extraction stops working.

4. **Legal** - Use for personal/educational purposes only. Respect game creators' work.
