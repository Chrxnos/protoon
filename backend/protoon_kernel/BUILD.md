# Protoon Kernel Driver Build Instructions

## Requirements

1. **Windows 10/11 x64**
2. **Visual Studio 2022** with:
   - Desktop development with C++
   - Windows 10/11 SDK
3. **Windows Driver Kit (WDK) 10** - Download from Microsoft
4. **Administrator privileges**

## Building the Driver

### Option 1: Visual Studio

1. Open Visual Studio 2022
2. Create new project: "Kernel Mode Driver, Empty (KMDF)"
3. Copy `driver.c` to the project
4. Build (Ctrl+Shift+B)
5. Output: `ProtoonDriver.sys`

### Option 2: Command Line

```powershell
# Open "Developer Command Prompt for VS 2022"
cd protoon_kernel

# Build driver
cl /c /Zp8 /MD /D "_AMD64_" /D "DEPRECATE_DDK_FUNCTIONS=1" driver.c
link /driver /subsystem:native /entry:DriverEntry /out:ProtoonDriver.sys driver.obj ntoskrnl.lib hal.lib wdmsec.lib
```

## Building the User-Mode Application

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Installing the Driver

### Step 1: Enable Test Signing

```powershell
# Run as Administrator
bcdedit /set testsigning on
# Reboot your computer
```

### Step 2: Disable Driver Signature Enforcement (if needed)

1. Hold Shift and click Restart
2. Troubleshoot > Advanced > Startup Settings > Restart
3. Press 7 (Disable driver signature enforcement)

### Step 3: Install Driver

```powershell
# Run as Administrator
sc create ProtoonDrv type= kernel binpath= "C:\full\path\to\ProtoonDriver.sys"
sc start ProtoonDrv
```

### Step 4: Verify Installation

```powershell
sc query ProtoonDrv
# Should show STATE: RUNNING
```

## Usage

1. Make sure Roblox is running and you're in a game
2. Run Protoon.exe as Administrator
3. Wait for extraction to complete
4. Open the generated .rbxlx file in Roblox Studio

## Troubleshooting

### "Driver not loaded"

The application will work without the driver, but Hyperion may detect and kick you.
Install the driver for undetected operation.

### "Roblox process not found"

Make sure RobloxPlayerBeta.exe is running, not the launcher.

### "Could not find DataModel"

The offsets may have changed. Update offsets.hpp from:
https://github.com/NtReadVirtualMemory/Roblox-Offsets-Website

### "Access denied"

Run as Administrator.

## Updating Offsets

Roblox updates frequently. Get the latest offsets from:
- https://github.com/NtReadVirtualMemory/Roblox-Offsets-Website
- https://robloxoffsets.pages.dev/

Update the values in `memory_reader.hpp` under `namespace RobloxOffsets`.

## Legal Notice

This tool is for educational purposes only. Using it to:
- Steal copyrighted game content
- Gain unfair advantages in games
- Violate Roblox Terms of Service

...may result in account bans or legal action. Use responsibly.
