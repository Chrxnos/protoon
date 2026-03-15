/*
 * Protoon Memory Reader - User-mode component
 * Communicates with kernel driver to read Roblox memory
 * 
 * This reads the DataModel from Roblox process memory
 * using kernel-level access beneath Hyperion
 */

#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

// IOCTL codes (must match driver)
#define IOCTL_PROTOON_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PROTOON_GET_MODULE_BASE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PROTOON_GET_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Roblox Memory Offsets (from NtReadVirtualMemory/Roblox-Offsets-Website)
// Updated: March 2026, Version: version-b130242ed064436f
namespace RobloxOffsets {
    // DataModel access
    constexpr uintptr_t VisualEnginePointer = 0x7D78148;
    constexpr uintptr_t VisualEngineToDataModel1 = 0xA50;
    constexpr uintptr_t VisualEngineToDataModel2 = 0x1C0;
    constexpr uintptr_t FakeDataModelPointer = 0x81C2C38;
    constexpr uintptr_t FakeDataModelToDataModel = 0x1C0;
    
    // Instance structure
    constexpr uintptr_t Children = 0x70;
    constexpr uintptr_t ChildrenEnd = 0x8;
    constexpr uintptr_t Parent = 0x68;
    constexpr uintptr_t Name = 0xB0;
    constexpr uintptr_t NameSize = 0x10;
    constexpr uintptr_t ClassDescriptor = 0x18;
    constexpr uintptr_t ClassDescriptorToClassName = 0x8;
    
    // Common properties
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
    
    // Services
    constexpr uintptr_t Workspace = 0x178;
    constexpr uintptr_t LocalPlayer = 0x130;
    constexpr uintptr_t Camera = 0x468;
    
    // Player/Character
    constexpr uintptr_t Health = 0x194;
    constexpr uintptr_t MaxHealth = 0x1B4;
    constexpr uintptr_t WalkSpeed = 0x1D4;
    constexpr uintptr_t JumpPower = 0x1B0;
}

// Communication structures
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

// Instance data from memory
struct MemoryInstance {
    uintptr_t address;
    std::string className;
    std::string name;
    uintptr_t parent;
    std::vector<uintptr_t> children;
    
    // Part-specific
    float position[3] = {0};
    float size[3] = {0};
    float transparency = 0;
    bool anchored = false;
    bool canCollide = true;
    uint8_t material = 0;
};

class ProtoonMemoryReader {
private:
    HANDLE hDriver = INVALID_HANDLE_VALUE;
    DWORD robloxPid = 0;
    uintptr_t robloxBase = 0;
    uintptr_t dataModel = 0;
    
    bool useKernelDriver = true;
    
public:
    ProtoonMemoryReader() {}
    
    ~ProtoonMemoryReader() {
        if (hDriver != INVALID_HANDLE_VALUE) {
            CloseHandle(hDriver);
        }
    }
    
    // Initialize connection to driver
    bool Initialize() {
        // Try kernel driver first
        hDriver = CreateFileW(
            L"\\\\.\\ProtoonDriver",
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        
        if (hDriver == INVALID_HANDLE_VALUE) {
            // Fall back to user-mode reading (will be detected by Hyperion)
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
        printf("[Protoon] Found Roblox PID: %d\n", robloxPid);
        
        // Get module base
        robloxBase = GetRobloxBase();
        if (robloxBase == 0) {
            printf("[Protoon] Error: Could not get Roblox base address\n");
            return false;
        }
        printf("[Protoon] Roblox base: 0x%llX\n", robloxBase);
        
        // Find DataModel
        dataModel = FindDataModel();
        if (dataModel == 0) {
            printf("[Protoon] Error: Could not find DataModel\n");
            return false;
        }
        printf("[Protoon] DataModel: 0x%llX\n", dataModel);
        
        return true;
    }
    
    // Read memory from Roblox process
    bool ReadMemory(uintptr_t address, void* buffer, size_t size) {
        if (useKernelDriver && hDriver != INVALID_HANDLE_VALUE) {
            MemoryReadRequest request;
            request.ProcessId = robloxPid;
            request.Address = address;
            request.Size = size;
            request.Buffer = buffer;
            
            DWORD bytesReturned;
            return DeviceIoControl(
                hDriver,
                IOCTL_PROTOON_READ_MEMORY,
                &request,
                sizeof(request),
                &request,
                sizeof(request),
                &bytesReturned,
                nullptr
            );
        } else {
            // User-mode fallback (will trigger Hyperion)
            HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, robloxPid);
            if (hProcess == nullptr) return false;
            
            SIZE_T bytesRead;
            bool result = ReadProcessMemory(hProcess, (LPCVOID)address, buffer, size, &bytesRead);
            CloseHandle(hProcess);
            return result && bytesRead == size;
        }
    }
    
    // Template read helpers
    template<typename T>
    T Read(uintptr_t address) {
        T value = {};
        ReadMemory(address, &value, sizeof(T));
        return value;
    }
    
    std::string ReadString(uintptr_t address, size_t maxLength = 256) {
        // Roblox strings: if length > 15, pointer at offset 0, else inline
        uintptr_t stringPtr = Read<uintptr_t>(address);
        size_t length = Read<size_t>(address + RobloxOffsets::NameSize);
        
        if (length == 0 || length > maxLength) return "";
        
        std::string result(length, '\0');
        
        if (length > 15) {
            // String is heap allocated
            ReadMemory(stringPtr, result.data(), length);
        } else {
            // String is inline (SSO)
            ReadMemory(address, result.data(), length);
        }
        
        return result;
    }
    
    // Find Roblox process
    DWORD FindRobloxProcess() {
        if (useKernelDriver && hDriver != INVALID_HANDLE_VALUE) {
            DWORD pid = 0;
            DWORD bytesReturned;
            DeviceIoControl(hDriver, IOCTL_PROTOON_GET_PID, nullptr, 0, &pid, sizeof(pid), &bytesReturned, nullptr);
            return pid;
        }
        
        // User-mode fallback
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
        
        // User-mode fallback
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
        if (visualEngine) {
            uintptr_t dm1 = Read<uintptr_t>(visualEngine + RobloxOffsets::VisualEngineToDataModel1);
            if (dm1) {
                uintptr_t dm = Read<uintptr_t>(dm1 + RobloxOffsets::VisualEngineToDataModel2);
                if (dm) return dm;
            }
        }
        
        // Method 2: Via FakeDataModel pointer
        uintptr_t fakeDataModel = Read<uintptr_t>(robloxBase + RobloxOffsets::FakeDataModelPointer);
        if (fakeDataModel) {
            uintptr_t dm = Read<uintptr_t>(fakeDataModel + RobloxOffsets::FakeDataModelToDataModel);
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
    
    // Get children of instance
    std::vector<uintptr_t> GetChildren(uintptr_t instance) {
        std::vector<uintptr_t> children;
        
        uintptr_t childrenStart = Read<uintptr_t>(instance + RobloxOffsets::Children);
        uintptr_t childrenEnd = Read<uintptr_t>(instance + RobloxOffsets::Children + RobloxOffsets::ChildrenEnd);
        
        if (childrenStart == 0 || childrenEnd == 0) return children;
        
        size_t count = (childrenEnd - childrenStart) / sizeof(uintptr_t);
        if (count > 10000) return children; // Sanity check
        
        for (size_t i = 0; i < count; i++) {
            uintptr_t child = Read<uintptr_t>(childrenStart + i * sizeof(uintptr_t));
            if (child) {
                children.push_back(child);
            }
        }
        
        return children;
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
            inst.className == "WedgePart" || inst.className == "SpawnLocation") {
            
            // Position from CFrame
            inst.position[0] = Read<float>(address + RobloxOffsets::CFrame + 0x0);
            inst.position[1] = Read<float>(address + RobloxOffsets::CFrame + 0x4);
            inst.position[2] = Read<float>(address + RobloxOffsets::CFrame + 0x8);
            
            // Size
            inst.size[0] = Read<float>(address + RobloxOffsets::PartSize + 0x0);
            inst.size[1] = Read<float>(address + RobloxOffsets::PartSize + 0x4);
            inst.size[2] = Read<float>(address + RobloxOffsets::PartSize + 0x8);
            
            inst.transparency = Read<float>(address + RobloxOffsets::Transparency);
            
            uint8_t flags = Read<uint8_t>(address + RobloxOffsets::Anchored);
            inst.anchored = (flags & RobloxOffsets::AnchoredMask) != 0;
            inst.canCollide = (flags & RobloxOffsets::CanCollideMask) != 0;
            
            inst.material = Read<uint8_t>(address + RobloxOffsets::MaterialType);
        }
        
        return inst;
    }
    
    // Get Workspace
    uintptr_t GetWorkspace() {
        return Read<uintptr_t>(dataModel + RobloxOffsets::Workspace);
    }
    
    // Recursively get all instances
    void GetAllInstances(uintptr_t instance, std::vector<MemoryInstance>& instances, int depth = 0) {
        if (instance == 0 || depth > 50) return; // Max depth to prevent infinite loops
        
        MemoryInstance inst = ReadInstance(instance);
        instances.push_back(inst);
        
        for (uintptr_t child : inst.children) {
            GetAllInstances(child, instances, depth + 1);
        }
    }
    
    // Main extraction function
    std::vector<MemoryInstance> ExtractMap() {
        std::vector<MemoryInstance> instances;
        
        uintptr_t workspace = GetWorkspace();
        if (workspace) {
            printf("[Protoon] Extracting from Workspace: 0x%llX\n", workspace);
            GetAllInstances(workspace, instances);
        }
        
        printf("[Protoon] Extracted %zu instances\n", instances.size());
        return instances;
    }
    
    // Getters
    uintptr_t GetDataModel() const { return dataModel; }
    DWORD GetPid() const { return robloxPid; }
    uintptr_t GetBase() const { return robloxBase; }
};
