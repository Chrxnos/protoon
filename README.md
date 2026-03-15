# Protoon

Kernel-level Roblox map extractor that operates beneath Hyperion anti-cheat.

## Quick Start

1. Download the latest release from [Releases](../../releases)
2. Extract the ZIP
3. Right-click `install.bat` → **Run as Administrator**
4. Join a Roblox game
5. Run `C:\Protoon\Protoon.exe` (as Admin)
6. Open the `.rbxlx` file in Roblox Studio

## How It Works

```
Roblox Game → Kernel Driver → Memory Read → DataModel → .rbxlx → Roblox Studio
                    ↓
           (Beneath Hyperion)
```

- **Kernel driver** operates at ring 0, below Hyperion's user-mode detection
- **Memory reader** uses current offsets from community research
- **Exports** to standard `.rbxlx` format compatible with Roblox Studio

## Requirements

- Windows 10/11 x64
- Administrator privileges
- Test signing mode enabled (installer handles this)

## Building from Source

See [BUILD.md](backend/protoon_kernel/BUILD.md) for compilation instructions.

## Offsets

Offsets are sourced from [NtReadVirtualMemory/Roblox-Offsets-Website](https://github.com/NtReadVirtualMemory/Roblox-Offsets-Website) and updated with each Roblox version.

Current version: `version-b130242ed064436f`

## License

Educational/personal use only. Not affiliated with Roblox Corporation.
