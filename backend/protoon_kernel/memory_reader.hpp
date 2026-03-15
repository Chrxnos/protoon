/*
 * Protoon Memory Reader - User-mode component
 * Communicates with kernel driver to read Roblox memory
 * 
 * This reads the DataModel from Roblox process memory
 * using kernel-level access beneath Hyperion
 * 
 * v1.1.0 - Enhanced with:
 *   - Debug logging for troubleshooting
 *   - Multiple traversal methods for instance tree
 *   - Asset reference reading (textures, sounds, animations, meshes)
 *   - Game info reading (PlaceId, GameId)
 */

#pragma once
#include <Windows.h>
#include <winioctl.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <algorithm>

// IOCTL codes (must match driver)
#define IOCTL_PROTOON_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PROTOON_GET_MODULE_BASE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PROTOON_GET_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Roblox Memory Offsets (from NtReadVirtualMemory/Roblox-Offsets-Website)
// Updated: March 2026, Version: version-b130242ed064436f
namespace RobloxOffsets {
    // DataModel access (pointer chain from base)
    constexpr uintptr_t VisualEnginePointer = 0x7D78148;
    constexpr uintptr_t VisualEngineToDataModel1 = 0xA50;
    constexpr uintptr_t VisualEngineToDataModel2 = 0x1C0;
    constexpr uintptr_t FakeDataModelPointer = 0x81C2C38;
    constexpr uintptr_t FakeDataModelToDataModel = 0x1C0;
    
    // Instance structure offsets
    constexpr uintptr_t Children = 0x70;        // std::vector<Instance*> begin
    constexpr uintptr_t ChildrenEnd = 0x8;      // relative to Children (end ptr)
    constexpr uintptr_t Parent = 0x68;
    constexpr uintptr_t Name = 0xB0;
    constexpr uintptr_t NameSize = 0x10;        // relative to any string
    constexpr uintptr_t ClassDescriptor = 0x18;
    constexpr uintptr_t ClassDescriptorToClassName = 0x8;
    
    // BasePart properties
    constexpr uintptr_t CFrame = 0xC0;
    constexpr uintptr_t Position = 0xE4;
    constexpr uintptr_t Rotation = 0xC8;
    constexpr uintptr_t PartSize = 0x1B0;
    constexpr uintptr_t Transparency = 0xF0;
    constexpr uintptr_t Anchored = 0x1AE;
    constexpr uintptr_t AnchoredMask = 0x2;
    constexpr uintptr_t CanCollide = 0x1AE;
    constexpr uintptr_t CanCollideMask = 0x8;
    constexpr uintptr_t MaterialType = 0x246;
    constexpr uintptr_t Primitive = 0x148;
    
    // Color (on BasePart via Primitive)
    constexpr uintptr_t MeshPartColor3 = 0x194;
    constexpr uintptr_t MeshPartTexture = 0x318;
    
    // Services from DataModel
    constexpr uintptr_t Workspace = 0x178;
    constexpr uintptr_t LocalPlayer = 0x130;
    constexpr uintptr_t Camera = 0x468;
    
    // DataModel properties
    constexpr uintptr_t CreatorId = 0x188;
    constexpr uintptr_t GameId = 0x190;
    constexpr uintptr_t PlaceId = 0x198;
    constexpr uintptr_t GameLoaded = 0x638;
    
    // Asset-related offsets (Content strings)
    constexpr uintptr_t DecalTexture = 0x198;     // on Decal/Texture instances
    constexpr uintptr_t SoundId = 0xE0;           // on Sound instances
    constexpr uintptr_t AnimationId = 0xD0;       // on Animation instances
    
    // Sky textures
    constexpr uintptr_t SkyboxBk = 0x110;
    constexpr uintptr_t SkyboxDn = 0x140;
    constexpr uintptr_t SkyboxFt = 0x170;
    constexpr uintptr_t SkyboxLf = 0x1A0;
    constexpr uintptr_t SkyboxRt = 0x1D0;
    constexpr uintptr_t SkyboxUp = 0x200;
    constexpr uintptr_t SunTextureId = 0x230;
    constexpr uintptr_t MoonTextureId = 0xE0;
    
    // Humanoid
    constexpr uintptr_t Health = 0x194;
    constexpr uintptr_t MaxHealth = 0x1B4;
    constexpr uintptr_t WalkSpeed = 0x1D4;
    constexpr uintptr_t JumpPower = 0x1B0;
}

// Communication structures for kernel driver
#pragma pack(push, 1)
struct MemoryReadRequest {
    ULONG ProcessId;
    ULONG_PTR Address;
    SIZE_T Size;
    void* Buffer;
};

struct ModuleBaseRequest {
    ULONG ProcessId;
    ULONG_PTR BaseAddress;
};
#pragma pack(pop)

// Asset reference found in memory
struct AssetReference {
    std::string assetId;      // Numeric asset ID
    std::string category;     // "Image", "Audio", "Mesh", "Animation", "Decal", "Sky"
    std::string sourceName;   // Name of instance that references this asset
    std::string sourceClass;  // ClassName of that instance
    std::string rawUrl;       // Raw content URL from memory
};

// Instance data from memory
struct MemoryInstance {
    uintptr_t address;
    std::string className;
    std::string name;
    uintptr_t parent;
    std::vector<uintptr_t> children;
    
    // Part-specific properties
    float position[3] = {0};
    float rotation[9] = {1,0,0, 0,1,0, 0,0,1}; // 3x3 rotation matrix
    float size[3] = {0};
    float transparency = 0;
    bool anchored = false;
    bool canCollide = true;
    uint8_t material = 0;
    
    // Color
    float color[3] = {0.639f, 0.635f, 0.647f}; // Default Medium stone grey
    
    // Asset references found on this instance
    std::vector<AssetReference> assetRefs;
};

class ProtoonMemoryReader {
private:
    HANDLE hDriver = INVALID_HANDLE_VALUE;
    DWORD robloxPid = 0;
    uintptr_t robloxBase = 0;
    uintptr_t dataModel = 0;
    
    bool useKernelDriver = true;
    HANDLE hProcess = nullptr; // Cached process handle for user-mode
    
    int totalReads = 0;
    int failedReads = 0;
    
public:
    bool debugMode = false;
    
    ProtoonMemoryReader() {}
    
    ~ProtoonMemoryReader() {
        if (hDriver != INVALID_HANDLE_VALUE) {
            CloseHandle(hDriver);
        }
        if (hProcess) {
            CloseHandle(hProcess);
        }
    }
    
    // Initialize connection
    bool Initialize() {
        // Try kernel driver first
        hDriver = CreateFileW(
            L"\\\\.\\ProtoonDriver",
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr
        );
        
        if (hDriver == INVALID_HANDLE_VALUE) {
            useKernelDriver = false;
            printf("[Protoon] Warning: Kernel driver not loaded, using user-mode (may be detected)\n");
        } else {
            printf("[Protoon] Kernel driver connected\n");
        }
        
        // Find Roblox process
        robloxPid = FindRobloxProcess();
        if (robloxPid == 0) {
            printf("[Protoon] Error: Roblox process not found\n");
            return false;
        }
        printf("[Protoon] Found Roblox PID: %lu\n", robloxPid);
        
        // Open process handle for user-mode (cache it to avoid repeated OpenProcess calls)
        if (!useKernelDriver) {
            hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, robloxPid);
            if (!hProcess) {
                printf("[Protoon] Error: Cannot open Roblox process (run as Administrator)\n");
                return false;
            }
        }
        
        // Get module base
        robloxBase = GetRobloxBase();
        if (robloxBase == 0) {
            printf("[Protoon] Error: Could not get Roblox base address\n");
            return false;
        }
        printf("[Protoon] Roblox base: 0x%llX\n", (unsigned long long)robloxBase);
        
        // Find DataModel
        dataModel = FindDataModel();
        if (dataModel == 0) {
            printf("[Protoon] Error: Could not find DataModel (are you in a game?)\n");
            return false;
        }
        printf("[Protoon] DataModel: 0x%llX\n", (unsigned long long)dataModel);
        
        return true;
    }
    
    // Read memory from Roblox process
    bool ReadMemory(uintptr_t address, void* buffer, size_t size) {
        totalReads++;
        
        if (address == 0 || address < 0x10000) {
            failedReads++;
            return false;
        }
        
        if (useKernelDriver && hDriver != INVALID_HANDLE_VALUE) {
            MemoryReadRequest request;
            request.ProcessId = robloxPid;
            request.Address = address;
            request.Size = size;
            request.Buffer = buffer;
            
            DWORD bytesReturned;
            BOOL result = DeviceIoControl(
                hDriver, IOCTL_PROTOON_READ_MEMORY,
                &request, sizeof(request),
                &request, sizeof(request),
                &bytesReturned, nullptr
            );
            if (!result) failedReads++;
            return result != 0;
        } else if (hProcess) {
            SIZE_T bytesRead;
            BOOL result = ReadProcessMemory(hProcess, (LPCVOID)address, buffer, size, &bytesRead);
            if (!result || bytesRead != size) {
                failedReads++;
                return false;
            }
            return true;
        }
        
        failedReads++;
        return false;
    }
    
    // Template read helper with error tracking
    template<typename T>
    T Read(uintptr_t address) {
        T value = {};
        ReadMemory(address, &value, sizeof(T));
        return value;
    }
    
    // Read Roblox std::string (handles SSO)
    std::string ReadString(uintptr_t address, size_t maxLength = 512) {
        uintptr_t stringPtr = Read<uintptr_t>(address);
        size_t length = Read<size_t>(address + RobloxOffsets::NameSize);
        
        if (length == 0 || length > maxLength) return "";
        
        std::string result(length, '\0');
        
        if (length > 15) {
            // Heap allocated string
            if (!ReadMemory(stringPtr, result.data(), length)) return "";
        } else {
            // SSO inline string
            if (!ReadMemory(address, result.data(), length)) return "";
        }
        
        // Validate: only printable ASCII
        for (char c : result) {
            if (c != 0 && (c < 32 || c > 126)) return "";
        }
        
        return result;
    }
    
    // Read a Content property (asset URL string)
    std::string ReadContentString(uintptr_t instance, uintptr_t offset) {
        return ReadString(instance + offset);
    }
    
    // Extract numeric asset ID from various URL formats
    static std::string ExtractAssetId(const std::string& content) {
        if (content.empty()) return "";
        
        // "rbxassetid://12345678"
        size_t pos = content.find("://");
        if (pos != std::string::npos) {
            pos += 3;
            std::string id;
            while (pos < content.size() && content[pos] >= '0' && content[pos] <= '9') {
                id += content[pos++];
            }
            if (id.size() >= 4) return id;
        }
        
        // "?id=12345678"
        pos = content.find("id=");
        if (pos != std::string::npos) {
            pos += 3;
            std::string id;
            while (pos < content.size() && content[pos] >= '0' && content[pos] <= '9') {
                id += content[pos++];
            }
            if (id.size() >= 4) return id;
        }
        
        // Pure number
        bool allDigits = true;
        for (char c : content) {
            if (c < '0' || c > '9') { allDigits = false; break; }
        }
        if (allDigits && content.size() >= 4) return content;
        
        return "";
    }
    
    // Find Roblox process
    DWORD FindRobloxProcess() {
        if (useKernelDriver && hDriver != INVALID_HANDLE_VALUE) {
            DWORD pid = 0;
            DWORD bytesReturned;
            DeviceIoControl(hDriver, IOCTL_PROTOON_GET_PID, nullptr, 0, &pid, sizeof(pid), &bytesReturned, nullptr);
            return pid;
        }
        
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;
        
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (wcsstr(entry.szExeFile, L"RobloxPlayerBeta") != nullptr) {
                    CloseHandle(snapshot);
                    return entry.th32ProcessID;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        
        CloseHandle(snapshot);
        return 0;
    }
    
    // Get Roblox base address
    uintptr_t GetRobloxBase() {
        if (useKernelDriver && hDriver != INVALID_HANDLE_VALUE) {
            ModuleBaseRequest request;
            request.ProcessId = robloxPid;
            request.BaseAddress = 0;
            
            DWORD bytesReturned;
            DeviceIoControl(hDriver, IOCTL_PROTOON_GET_MODULE_BASE, &request, sizeof(request), &request, sizeof(request), &bytesReturned, nullptr);
            return request.BaseAddress;
        }
        
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, robloxPid);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;
        
        MODULEENTRY32W entry;
        entry.dwSize = sizeof(entry);
        
        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (wcsstr(entry.szModule, L"RobloxPlayerBeta") != nullptr) {
                    CloseHandle(snapshot);
                    return (uintptr_t)entry.modBaseAddr;
                }
            } while (Module32NextW(snapshot, &entry));
        }
        
        CloseHandle(snapshot);
        return 0;
    }
    
    // Find DataModel using multiple methods
    uintptr_t FindDataModel() {
        // Method 1: Via VisualEngine pointer
        uintptr_t visualEngine = Read<uintptr_t>(robloxBase + RobloxOffsets::VisualEnginePointer);
        if (debugMode) printf("[DEBUG] VisualEngine ptr: 0x%llX\n", (unsigned long long)visualEngine);
        
        if (visualEngine) {
            uintptr_t dm1 = Read<uintptr_t>(visualEngine + RobloxOffsets::VisualEngineToDataModel1);
            if (debugMode) printf("[DEBUG] VisualEngine->DM1: 0x%llX\n", (unsigned long long)dm1);
            if (dm1) {
                uintptr_t dm = Read<uintptr_t>(dm1 + RobloxOffsets::VisualEngineToDataModel2);
                if (debugMode) printf("[DEBUG] DM1->DataModel: 0x%llX\n", (unsigned long long)dm);
                if (dm) return dm;
            }
        }
        
        // Method 2: Via FakeDataModel pointer
        uintptr_t fakeDataModel = Read<uintptr_t>(robloxBase + RobloxOffsets::FakeDataModelPointer);
        if (debugMode) printf("[DEBUG] FakeDataModel ptr: 0x%llX\n", (unsigned long long)fakeDataModel);
        if (fakeDataModel) {
            uintptr_t dm = Read<uintptr_t>(fakeDataModel + RobloxOffsets::FakeDataModelToDataModel);
            if (debugMode) printf("[DEBUG] FakeDM->DataModel: 0x%llX\n", (unsigned long long)dm);
            if (dm) return dm;
        }
        
        return 0;
    }
    
    // Get instance class name
    std::string GetClassName(uintptr_t instance) {
        uintptr_t classDescriptor = Read<uintptr_t>(instance + RobloxOffsets::ClassDescriptor);
        if (classDescriptor == 0) return "";
        
        uintptr_t classNamePtr = Read<uintptr_t>(classDescriptor + RobloxOffsets::ClassDescriptorToClassName);
        if (classNamePtr == 0) return "";
        
        return ReadString(classNamePtr);
    }
    
    // Get instance name
    std::string GetName(uintptr_t instance) {
        return ReadString(instance + RobloxOffsets::Name);
    }
    
    // Get children of instance with enhanced logging
    std::vector<uintptr_t> GetChildren(uintptr_t instance) {
        std::vector<uintptr_t> children;
        
        uintptr_t childrenStart = Read<uintptr_t>(instance + RobloxOffsets::Children);
        uintptr_t childrenEnd = Read<uintptr_t>(instance + RobloxOffsets::Children + RobloxOffsets::ChildrenEnd);
        
        if (debugMode) {
            printf("[DEBUG] GetChildren(0x%llX):\n", (unsigned long long)instance);
            printf("[DEBUG]   children_begin: 0x%llX\n", (unsigned long long)childrenStart);
            printf("[DEBUG]   children_end:   0x%llX\n", (unsigned long long)childrenEnd);
        }
        
        if (childrenStart == 0 || childrenEnd == 0) {
            if (debugMode) printf("[DEBUG]   -> Null pointer, no children\n");
            return children;
        }
        
        if (childrenEnd <= childrenStart) {
            if (debugMode) printf("[DEBUG]   -> end <= start, no children\n");
            return children;
        }
        
        size_t count = (childrenEnd - childrenStart) / sizeof(uintptr_t);
        if (debugMode) printf("[DEBUG]   -> Child count: %zu\n", count);
        
        if (count > 500000) {
            if (debugMode) printf("[DEBUG]   -> Count too large, bad data\n");
            return children;
        }
        
        // Read all child pointers in one batch for speed
        std::vector<uintptr_t> rawPtrs(count);
        if (ReadMemory(childrenStart, rawPtrs.data(), count * sizeof(uintptr_t))) {
            for (size_t i = 0; i < count; i++) {
                if (rawPtrs[i] > 0x10000) {
                    children.push_back(rawPtrs[i]);
                }
            }
        } else {
            // Fallback: read one by one
            if (debugMode) printf("[DEBUG]   -> Batch read failed, reading individually\n");
            for (size_t i = 0; i < count; i++) {
                uintptr_t child = Read<uintptr_t>(childrenStart + i * sizeof(uintptr_t));
                if (child > 0x10000) {
                    children.push_back(child);
                }
            }
        }
        
        if (debugMode) printf("[DEBUG]   -> Found %zu valid children\n", children.size());
        return children;
    }
    
    // Collect asset references from instance properties
    void CollectAssetRefs(MemoryInstance& inst) {
        // Decal / Texture instances
        if (inst.className == "Decal" || inst.className == "Texture") {
            std::string url = ReadContentString(inst.address, RobloxOffsets::DecalTexture);
            std::string id = ExtractAssetId(url);
            if (!id.empty()) {
                inst.assetRefs.push_back({id, "Decal", inst.name, inst.className, url});
            }
        }
        
        // MeshPart texture
        if (inst.className == "MeshPart") {
            std::string url = ReadContentString(inst.address, RobloxOffsets::MeshPartTexture);
            std::string id = ExtractAssetId(url);
            if (!id.empty()) {
                inst.assetRefs.push_back({id, "Image", inst.name, inst.className, url});
            }
        }
        
        // Sound
        if (inst.className == "Sound") {
            std::string url = ReadContentString(inst.address, RobloxOffsets::SoundId);
            std::string id = ExtractAssetId(url);
            if (!id.empty()) {
                inst.assetRefs.push_back({id, "Audio", inst.name, inst.className, url});
            }
        }
        
        // Animation
        if (inst.className == "Animation") {
            std::string url = ReadContentString(inst.address, RobloxOffsets::AnimationId);
            std::string id = ExtractAssetId(url);
            if (!id.empty()) {
                inst.assetRefs.push_back({id, "Animation", inst.name, inst.className, url});
            }
        }
        
        // Sky textures
        if (inst.className == "Sky") {
            struct { uintptr_t offset; const char* label; } skyOffsets[] = {
                {RobloxOffsets::SkyboxBk, "SkyboxBk"},
                {RobloxOffsets::SkyboxDn, "SkyboxDn"},
                {RobloxOffsets::SkyboxFt, "SkyboxFt"},
                {RobloxOffsets::SkyboxLf, "SkyboxLf"},
                {RobloxOffsets::SkyboxRt, "SkyboxRt"},
                {RobloxOffsets::SkyboxUp, "SkyboxUp"},
                {RobloxOffsets::SunTextureId, "SunTexture"},
                {RobloxOffsets::MoonTextureId, "MoonTexture"},
            };
            for (auto& s : skyOffsets) {
                std::string url = ReadContentString(inst.address, s.offset);
                std::string id = ExtractAssetId(url);
                if (!id.empty()) {
                    inst.assetRefs.push_back({id, "Sky", std::string(s.label), inst.className, url});
                }
            }
        }
    }
    
    // Read full instance data
    MemoryInstance ReadInstance(uintptr_t address) {
        MemoryInstance inst;
        inst.address = address;
        inst.className = GetClassName(address);
        inst.name = GetName(address);
        inst.parent = Read<uintptr_t>(address + RobloxOffsets::Parent);
        inst.children = GetChildren(address);
        
        // Read part-specific properties
        if (inst.className == "Part" || inst.className == "MeshPart" || 
            inst.className == "WedgePart" || inst.className == "SpawnLocation" ||
            inst.className == "TrussPart" || inst.className == "CornerWedgePart" ||
            inst.className == "UnionOperation" || inst.className == "NegateOperation" ||
            inst.className == "Seat" || inst.className == "VehicleSeat") {
            
            // Read Primitive pointer for CFrame
            uintptr_t primitive = Read<uintptr_t>(address + RobloxOffsets::Primitive);
            if (primitive) {
                // CFrame is stored on the primitive
                // Read full 12-float CFrame (3x3 rotation + 3 position)
                float cframe[12] = {0};
                ReadMemory(primitive + RobloxOffsets::CFrame, cframe, sizeof(cframe));
                
                // Rotation matrix (first 9 floats)
                for (int i = 0; i < 9; i++) inst.rotation[i] = cframe[i];
                // Position (last 3 floats)
                inst.position[0] = cframe[9];
                inst.position[1] = cframe[10];
                inst.position[2] = cframe[11];
            } else {
                // Fallback: try reading CFrame directly from instance
                inst.position[0] = Read<float>(address + RobloxOffsets::CFrame + 0x24);
                inst.position[1] = Read<float>(address + RobloxOffsets::CFrame + 0x28);
                inst.position[2] = Read<float>(address + RobloxOffsets::CFrame + 0x2C);
            }
            
            // Size
            inst.size[0] = Read<float>(address + RobloxOffsets::PartSize + 0x0);
            inst.size[1] = Read<float>(address + RobloxOffsets::PartSize + 0x4);
            inst.size[2] = Read<float>(address + RobloxOffsets::PartSize + 0x8);
            
            inst.transparency = Read<float>(address + RobloxOffsets::Transparency);
            
            uint8_t flags = Read<uint8_t>(address + RobloxOffsets::Anchored);
            inst.anchored = (flags & RobloxOffsets::AnchoredMask) != 0;
            inst.canCollide = (flags & RobloxOffsets::CanCollideMask) != 0;
            
            inst.material = Read<uint8_t>(address + RobloxOffsets::MaterialType);
            
            // Color for MeshPart
            if (inst.className == "MeshPart") {
                inst.color[0] = Read<float>(address + RobloxOffsets::MeshPartColor3 + 0x0);
                inst.color[1] = Read<float>(address + RobloxOffsets::MeshPartColor3 + 0x4);
                inst.color[2] = Read<float>(address + RobloxOffsets::MeshPartColor3 + 0x8);
            }
        }
        
        // Collect asset references
        CollectAssetRefs(inst);
        
        return inst;
    }
    
    // Get Workspace pointer
    uintptr_t GetWorkspace() {
        uintptr_t ws = Read<uintptr_t>(dataModel + RobloxOffsets::Workspace);
        if (debugMode) printf("[DEBUG] Workspace ptr from DataModel+0x178: 0x%llX\n", (unsigned long long)ws);
        return ws;
    }
    
    // Get DataModel children (services)
    std::vector<uintptr_t> GetDataModelChildren() {
        if (debugMode) printf("[DEBUG] Getting DataModel children...\n");
        return GetChildren(dataModel);
    }
    
    // Get PlaceId
    uint64_t GetPlaceId() {
        return Read<uint64_t>(dataModel + RobloxOffsets::PlaceId);
    }
    
    // Get GameId  
    uint64_t GetGameId() {
        return Read<uint64_t>(dataModel + RobloxOffsets::GameId);
    }
    
    // Get CreatorId
    uint64_t GetCreatorId() {
        return Read<uint64_t>(dataModel + RobloxOffsets::CreatorId);
    }
    
    // Check if game is loaded
    bool IsGameLoaded() {
        return Read<uint8_t>(dataModel + RobloxOffsets::GameLoaded) != 0;
    }
    
    // Recursively get all instances with progress callback
    void GetAllInstances(uintptr_t instance, std::vector<MemoryInstance>& instances, 
                         std::set<uintptr_t>& visited, int depth = 0, int maxDepth = 100) {
        if (instance == 0 || instance < 0x10000 || depth > maxDepth) return;
        
        // Prevent infinite loops
        if (visited.count(instance)) return;
        visited.insert(instance);
        
        MemoryInstance inst = ReadInstance(instance);
        
        // Validate - skip if we can't read basic properties
        if (inst.className.empty() && inst.name.empty()) {
            if (debugMode) printf("[DEBUG] Skipping invalid instance at 0x%llX\n", (unsigned long long)instance);
            return;
        }
        
        instances.push_back(inst);
        
        // Show progress for top-level items
        if (depth <= 2 && !inst.name.empty()) {
            printf("[Protoon]   %s%s (%s) - %zu children\n", 
                std::string(depth * 2, ' ').c_str(),
                inst.name.c_str(), inst.className.c_str(), inst.children.size());
        }
        
        for (uintptr_t child : inst.children) {
            GetAllInstances(child, instances, visited, depth + 1, maxDepth);
        }
    }
    
    // Main extraction - tries multiple methods
    std::vector<MemoryInstance> ExtractAll() {
        std::vector<MemoryInstance> instances;
        std::set<uintptr_t> visited;
        
        printf("[Protoon] Reading game structure...\n");
        
        // Method 1: Try DataModel children directly (gets ALL services)
        printf("[Protoon] Method 1: Enumerating DataModel services...\n");
        auto dmChildren = GetDataModelChildren();
        
        if (!dmChildren.empty()) {
            printf("[Protoon] Found %zu services in DataModel\n", dmChildren.size());
            
            for (uintptr_t serviceAddr : dmChildren) {
                std::string serviceName = GetName(serviceAddr);
                std::string serviceClass = GetClassName(serviceAddr);
                
                if (debugMode) {
                    printf("[DEBUG] Service: %s (%s) at 0x%llX\n", 
                        serviceName.c_str(), serviceClass.c_str(), (unsigned long long)serviceAddr);
                }
                
                // Recurse into each service
                GetAllInstances(serviceAddr, instances, visited, 0);
            }
        } else {
            printf("[Protoon] Method 1 found no DataModel children\n");
        }
        
        // Method 2: Try Workspace directly if not already visited
        uintptr_t workspace = GetWorkspace();
        if (workspace && !visited.count(workspace)) {
            printf("[Protoon] Method 2: Direct Workspace access...\n");
            GetAllInstances(workspace, instances, visited, 0);
        }
        
        // Summary
        printf("[Protoon] Total: %zu instances extracted\n", instances.size());
        if (debugMode) {
            printf("[DEBUG] Memory reads: %d total, %d failed (%.1f%% success)\n", 
                totalReads, failedReads, 
                totalReads > 0 ? (100.0 * (totalReads - failedReads) / totalReads) : 0.0);
        }
        
        return instances;
    }
    
    // Getters
    uintptr_t GetDataModel() const { return dataModel; }
    DWORD GetPid() const { return robloxPid; }
    uintptr_t GetBase() const { return robloxBase; }
    int GetTotalReads() const { return totalReads; }
    int GetFailedReads() const { return failedReads; }
};
