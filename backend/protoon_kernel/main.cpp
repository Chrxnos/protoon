/*
 * Protoon v1.1.0 - Roblox Asset & Map Extraction Tool
 * 
 * Features:
 *   - Full game instance tree extraction
 *   - Asset downloading (images, audio, meshes, animations, decals)
 *   - Organized Downloads folder per game
 *   - Fleasion-style scrape options
 *   - Kernel driver for undetected mode (optional)
 *   - Debug mode for troubleshooting
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <set>
#include <map>
#include <algorithm>
#include <chrono>

#include "memory_reader.hpp"

// For HTTP downloads
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// For SHGetFolderPath
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

// ============================================================
// Material name lookup
// ============================================================
const char* GetMaterialName(uint8_t material) {
    switch (material) {
        case 0: return "Plastic";
        case 1: return "SmoothPlastic"; case 2: return "Wood"; case 3: return "WoodPlanks";
        case 4: return "Marble"; case 5: return "Slate"; case 6: return "Concrete";
        case 7: return "Granite"; case 8: return "Brick"; case 9: return "Pebble";
        case 10: return "Cobblestone"; case 11: return "Rock"; case 12: return "Sandstone";
        case 13: return "Basalt"; case 14: return "CrackedLava"; case 15: return "Glacier";
        case 16: return "Snow"; case 17: return "Ice"; case 18: return "Glass";
        case 19: return "Metal"; case 20: return "CorrodedMetal"; case 21: return "DiamondPlate";
        case 22: return "Foil"; case 23: return "Grass"; case 24: return "LeafyGrass";
        case 25: return "Sand"; case 26: return "Fabric"; case 27: return "Neon";
        case 28: return "ForceField"; default: return "Plastic";
    }
}

// ============================================================
// HTTP Download using WinHTTP
// ============================================================
bool DownloadAsset(const std::string& assetId, const fs::path& outputPath) {
    std::wstring host = L"assetdelivery.roblox.com";
    std::wstring path = L"/v1/asset/?id=" + std::wstring(assetId.begin(), assetId.end());
    
    HINTERNET hSession = WinHttpOpen(L"Protoon/1.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    // Set timeouts
    WinHttpSetTimeouts(hSession, 5000, 10000, 10000, 30000);
    
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    
    // Enable auto-redirect
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }
    
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Check status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    
    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Read response
    std::vector<BYTE> data;
    DWORD bytesRead;
    BYTE buffer[8192];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        data.insert(data.end(), buffer, buffer + bytesRead);
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    if (data.empty()) return false;
    
    // Detect file extension from content
    std::string ext = ".bin";
    if (data.size() >= 4) {
        if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') ext = ".png";
        else if (data[0] == 0xFF && data[1] == 0xD8) ext = ".jpg";
        else if (data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S') ext = ".ogg";
        else if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') ext = ".mp3";
        else if (data[0] == 0xFF && data[1] == 0xFB) ext = ".mp3";
        else if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') ext = ".wav";
        else if (data.size() >= 8 && memcmp(data.data(), "version ", 8) == 0) ext = ".mesh";
        else if (data[0] == '<') ext = ".xml";
        else if (data[0] == 0xAB && data[1] == 'K' && data[2] == 'T' && data[3] == 'X') ext = ".ktx";
    }
    
    // Save file
    fs::path finalPath = outputPath;
    if (finalPath.extension().empty()) {
        finalPath += ext;
    }
    
    std::ofstream file(finalPath, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<char*>(data.data()), data.size());
    file.close();
    
    return true;
}

// ============================================================
// RBXLX Generation (improved)
// ============================================================
std::string GenerateRBXLX(const std::vector<MemoryInstance>& instances) {
    std::ostringstream xml;
    
    xml << R"(<?xml version="1.0" encoding="utf-8"?>)" << "\n";
    xml << R"(<roblox xmlns:xmime="http://www.w3.org/2005/05/xmlmime" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.roblox.com/roblox.xsd" version="4">)" << "\n";
    xml << "  <External>null</External>\n";
    xml << "  <External>nil</External>\n";
    
    // Build parent->children map
    std::unordered_map<uintptr_t, std::vector<const MemoryInstance*>> childrenMap;
    std::unordered_map<uintptr_t, const MemoryInstance*> instanceMap;
    
    for (const auto& inst : instances) {
        instanceMap[inst.address] = &inst;
        childrenMap[inst.parent].push_back(&inst);
    }
    
    // Set of part-like classes
    static const std::set<std::string> partClasses = {
        "Part", "MeshPart", "WedgePart", "SpawnLocation", "TrussPart",
        "CornerWedgePart", "UnionOperation", "NegateOperation", "Seat", "VehicleSeat"
    };
    
    // Recursive writer
    int refCounter = 1;
    std::function<void(const MemoryInstance*, int)> writeInstance = [&](const MemoryInstance* inst, int indent) {
        std::string tabs(indent * 2, ' ');
        std::string ref = "RBX" + std::to_string(refCounter++);
        
        xml << tabs << "<Item class=\"" << inst->className << "\" referent=\"" << ref << "\">\n";
        xml << tabs << "  <Properties>\n";
        
        // Name
        if (!inst->name.empty()) {
            xml << tabs << "    <string name=\"Name\">" << inst->name << "</string>\n";
        }
        
        // Part-specific properties
        if (partClasses.count(inst->className)) {
            // Full CFrame with rotation
            xml << tabs << "    <CoordinateFrame name=\"CFrame\">\n";
            xml << tabs << "      <X>" << inst->position[0] << "</X>\n";
            xml << tabs << "      <Y>" << inst->position[1] << "</Y>\n";
            xml << tabs << "      <Z>" << inst->position[2] << "</Z>\n";
            xml << tabs << "      <R00>" << inst->rotation[0] << "</R00>";
            xml << "<R01>" << inst->rotation[1] << "</R01>";
            xml << "<R02>" << inst->rotation[2] << "</R02>\n";
            xml << tabs << "      <R10>" << inst->rotation[3] << "</R10>";
            xml << "<R11>" << inst->rotation[4] << "</R11>";
            xml << "<R12>" << inst->rotation[5] << "</R12>\n";
            xml << tabs << "      <R20>" << inst->rotation[6] << "</R20>";
            xml << "<R21>" << inst->rotation[7] << "</R21>";
            xml << "<R22>" << inst->rotation[8] << "</R22>\n";
            xml << tabs << "    </CoordinateFrame>\n";
            
            // Size
            xml << tabs << "    <Vector3 name=\"size\">\n";
            xml << tabs << "      <X>" << inst->size[0] << "</X>\n";
            xml << tabs << "      <Y>" << inst->size[1] << "</Y>\n";
            xml << tabs << "      <Z>" << inst->size[2] << "</Z>\n";
            xml << tabs << "    </Vector3>\n";
            
            xml << tabs << "    <bool name=\"Anchored\">" << (inst->anchored ? "true" : "false") << "</bool>\n";
            xml << tabs << "    <bool name=\"CanCollide\">" << (inst->canCollide ? "true" : "false") << "</bool>\n";
            xml << tabs << "    <float name=\"Transparency\">" << inst->transparency << "</float>\n";
            xml << tabs << "    <token name=\"Material\">" << GetMaterialName(inst->material) << "</token>\n";
            
            // Color
            xml << tabs << "    <Color3 name=\"Color\">\n";
            xml << tabs << "      <R>" << inst->color[0] << "</R>\n";
            xml << tabs << "      <G>" << inst->color[1] << "</G>\n";
            xml << tabs << "      <B>" << inst->color[2] << "</B>\n";
            xml << tabs << "    </Color3>\n";
        }
        
        xml << tabs << "  </Properties>\n";
        
        // Write children
        auto it = childrenMap.find(inst->address);
        if (it != childrenMap.end()) {
            for (const auto* child : it->second) {
                writeInstance(child, indent + 1);
            }
        }
        
        xml << tabs << "</Item>\n";
    };
    
    // Find root instances (those whose parent is not in our set)
    for (const auto& inst : instances) {
        if (instanceMap.find(inst.parent) == instanceMap.end()) {
            writeInstance(&inst, 1);
        }
    }
    
    xml << "</roblox>\n";
    return xml.str();
}

// ============================================================
// UI Helpers
// ============================================================
void PrintBanner() {
    std::cout << R"(
 ____            _                   
|  _ \ _ __ ___ | |_ ___   ___  _ __  
| |_) | '__/ _ \| __/ _ \ / _ \| '_ \ 
|  __/| | | (_) | || (_) | (_) | | | |
|_|   |_|  \___/ \__\___/ \___/|_| |_|
                                      
    Roblox Asset & Map Extraction Tool
    v1.3.0 - Asset & Map Extraction
    )" << std::endl;
}

void WaitForExit() {
    std::cout << "\nPress Enter to exit...";
    std::cin.ignore(10000, '\n');
    std::cin.get();
}

void PrintHelp() {
    std::cout << "Usage: Protoon.exe [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --debug      Enable debug mode (verbose output)\n";
    std::cout << "  --output DIR Set output directory (default: ./Downloads)\n";
    std::cout << "  --help       Show this help message\n";
    std::cout << std::endl;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    PrintBanner();
    
    // Parse arguments
    bool debugMode = false;  // Off by default now (Method B confirmed working)
    std::string outputDir = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--debug") debugMode = true;
        else if (arg == "--quiet") debugMode = false;
        else if (arg == "--output" && i + 1 < argc) outputDir = argv[++i];
        else if (arg == "--help") { PrintHelp(); return 0; }
    }
    
    // Initialize
    std::cout << "[*] Initializing Protoon...\n";
    
    ProtoonMemoryReader reader;
    reader.debugMode = debugMode;
    
    if (!reader.Initialize()) {
        std::cout << "\n[!] Failed to initialize. Make sure:\n";
        std::cout << "    1. Roblox is running (RobloxPlayerBeta.exe)\n";
        std::cout << "    2. You are IN A GAME (not just the launcher/menu)\n";
        std::cout << "    3. You're running as Administrator\n";
        std::cout << "    4. Wait for the game to fully load before running\n";
        std::cout << "\n    For undetected mode (kernel driver):\n";
        std::cout << "    bcdedit /set testsigning on\n";
        std::cout << "    (reboot)\n";
        std::cout << "    sc create ProtoonDrv type= kernel binpath= \"C:\\Protoon\\ProtoonDriver.sys\"\n";
        std::cout << "    sc start ProtoonDrv\n";
        WaitForExit();
        return 1;
    }
    
    // Get game info
    uint64_t placeId = reader.GetPlaceId();
    uint64_t gameId = reader.GetGameId();
    
    std::cout << "[*] Connected to Roblox (PID: " << reader.GetPid() << ")\n";
    std::cout << "[*] DataModel found at: 0x" << std::hex << reader.GetDataModel() << std::dec << "\n";
    std::cout << "[*] PlaceId: " << placeId << "\n";
    std::cout << "[*] GameId: " << gameId << "\n";
    
    // Diagnostic: quick check on children reading method
    if (debugMode) {
        uintptr_t dm = reader.GetDataModel();
        uintptr_t ws = reader.Read<uintptr_t>(dm + 0x178);
        
        std::cout << "\n[DEBUG] === Quick Diagnostics ===\n";
        std::cout << "[DEBUG] DataModel: 0x" << std::hex << dm << std::dec << "\n";
        std::cout << "[DEBUG] Workspace: 0x" << std::hex << ws << std::dec << "\n";
        
        if (ws > 0x10000) {
            // Check children container
            uintptr_t containerPtr = reader.Read<uintptr_t>(ws + 0x70);
            uintptr_t begin = reader.Read<uintptr_t>(containerPtr);
            uintptr_t end = reader.Read<uintptr_t>(containerPtr + 8);
            size_t count = (end > begin) ? (end - begin) / 8 : 0;
            printf("[DEBUG] WS children: container=0x%llX begin=0x%llX end=0x%llX count=%zu\n",
                (unsigned long long)containerPtr, (unsigned long long)begin, (unsigned long long)end, count);
        }
        
        std::cout << "[DEBUG] ========================\n\n";
    }
    
    // Show extraction menu (Fleasion-style scrape options)
    std::cout << "\n========================================\n";
    std::cout << "  What do you want to extract?\n";
    std::cout << "========================================\n";
    std::cout << "  [1] Map only (.rbxlx)\n";
    std::cout << "  [2] Decals / Images\n";
    std::cout << "  [3] Audio / Sounds\n";
    std::cout << "  [4] Animations\n";
    std::cout << "  [5] Meshes (MeshPart textures)\n";
    std::cout << "  [6] Sky Textures\n";
    std::cout << "  [7] All Assets (no map)\n";
    std::cout << "  [8] Everything (map + all assets)\n";
    std::cout << "========================================\n";
    std::cout << "  Enter choice (1-8): ";
    
    int choice = 0;
    std::cin >> choice;
    
    if (choice < 1 || choice > 8) {
        std::cout << "[!] Invalid choice. Defaulting to Everything (8).\n";
        choice = 8;
    }
    
    bool extractMap = (choice == 1 || choice == 8);
    bool extractDecals = (choice == 2 || choice == 7 || choice == 8);
    bool extractAudio = (choice == 3 || choice == 7 || choice == 8);
    bool extractAnimations = (choice == 4 || choice == 7 || choice == 8);
    bool extractMeshes = (choice == 5 || choice == 7 || choice == 8);
    bool extractSky = (choice == 6 || choice == 7 || choice == 8);
    
    // Create output directory
    std::string gameFolderName = "Game_" + std::to_string(placeId);
    if (outputDir.empty()) {
        outputDir = "Downloads";
    }
    
    fs::path baseDir = fs::path(outputDir) / gameFolderName;
    fs::create_directories(baseDir);
    
    if (extractDecals) fs::create_directories(baseDir / "Decals");
    if (extractAudio) fs::create_directories(baseDir / "Audio");
    if (extractAnimations) fs::create_directories(baseDir / "Animations");
    if (extractMeshes) fs::create_directories(baseDir / "Meshes");
    if (extractSky) fs::create_directories(baseDir / "Sky");
    
    std::cout << "\n[*] Output directory: " << fs::absolute(baseDir).string() << "\n";
    
    // Extract instances
    std::cout << "\n[*] Extracting game data...\n";
    auto startTime = std::chrono::steady_clock::now();
    
    auto instances = reader.ExtractAll();
    
    auto extractTime = std::chrono::steady_clock::now();
    double extractSeconds = std::chrono::duration<double>(extractTime - startTime).count();
    
    if (instances.empty()) {
        std::cout << "[!] No instances extracted.\n";
        std::cout << "    This usually means:\n";
        std::cout << "    - The game hasn't fully loaded yet (wait a bit)\n";
        std::cout << "    - Memory offsets need updating\n";
        std::cout << "    - Anti-cheat blocked memory reading\n";
        if (!debugMode) {
            std::cout << "\n    Run with --debug for detailed diagnostics.\n";
        }
        WaitForExit();
        return 1;
    }
    
    // Count instances by type
    std::map<std::string, int> classCounts;
    int partCount = 0;
    std::vector<AssetReference> allAssets;
    
    for (const auto& inst : instances) {
        classCounts[inst.className]++;
        
        if (inst.className == "Part" || inst.className == "MeshPart" || 
            inst.className == "WedgePart" || inst.className == "UnionOperation") {
            partCount++;
        }
        
        // Collect all asset references
        for (const auto& ref : inst.assetRefs) {
            allAssets.push_back(ref);
        }
    }
    
    std::cout << "\n[*] Extraction complete in " << std::fixed << std::setprecision(1) << extractSeconds << "s\n";
    std::cout << "[*] Total instances: " << instances.size() << "\n";
    std::cout << "[*] Parts: " << partCount << "\n";
    
    // Show class breakdown
    std::cout << "\n[*] Instance breakdown:\n";
    std::vector<std::pair<std::string, int>> sorted(classCounts.begin(), classCounts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return b.second < a.second; });
    int shown = 0;
    for (const auto& [cls, count] : sorted) {
        if (shown++ > 15) { std::cout << "    ... and more\n"; break; }
        std::cout << "    " << std::setw(25) << std::left << cls << " " << count << "\n";
    }
    
    // Deduplicate assets
    std::set<std::string> seenAssets;
    std::map<std::string, std::vector<AssetReference>> assetsByCategory;
    
    for (const auto& ref : allAssets) {
        if (seenAssets.count(ref.assetId)) continue;
        seenAssets.insert(ref.assetId);
        assetsByCategory[ref.category].push_back(ref);
    }
    
    // Show asset summary
    std::cout << "\n[*] Asset references found:\n";
    int totalAssets = 0;
    for (const auto& [cat, refs] : assetsByCategory) {
        std::cout << "    " << std::setw(15) << std::left << cat << " " << refs.size() << " unique\n";
        totalAssets += (int)refs.size();
    }
    std::cout << "    " << std::setw(15) << std::left << "TOTAL" << " " << totalAssets << " unique assets\n";
    
    // Save map
    if (extractMap) {
        std::cout << "\n[*] Generating RBXLX map file...\n";
        std::string xml = GenerateRBXLX(instances);
        
        fs::path mapPath = baseDir / "Map.rbxlx";
        std::ofstream mapFile(mapPath);
        if (mapFile) {
            mapFile << xml;
            mapFile.close();
            std::cout << "[+] Map saved: " << mapPath.string() << "\n";
            std::cout << "[+] " << instances.size() << " instances, " << partCount << " parts\n";
        } else {
            std::cout << "[!] Failed to save map file\n";
        }
    }
    
    // Download assets
    bool anyDownload = extractDecals || extractAudio || extractAnimations || extractMeshes || extractSky;
    
    if (anyDownload && totalAssets > 0) {
        std::cout << "\n[*] Downloading assets from Roblox CDN...\n";
        
        int downloaded = 0;
        int failed = 0;
        int skipped = 0;
        
        for (const auto& [category, refs] : assetsByCategory) {
            // Check if this category is selected
            bool shouldDownload = false;
            fs::path categoryDir;
            
            if (category == "Decal" || category == "Image") {
                shouldDownload = extractDecals;
                categoryDir = baseDir / "Decals";
            } else if (category == "Audio") {
                shouldDownload = extractAudio;
                categoryDir = baseDir / "Audio";
            } else if (category == "Animation") {
                shouldDownload = extractAnimations;
                categoryDir = baseDir / "Animations";
            } else if (category == "Mesh") {
                shouldDownload = extractMeshes;
                categoryDir = baseDir / "Meshes";
            } else if (category == "Sky") {
                shouldDownload = extractSky;
                categoryDir = baseDir / "Sky";
            }
            
            if (!shouldDownload) {
                skipped += (int)refs.size();
                continue;
            }
            
            fs::create_directories(categoryDir);
            
            for (const auto& ref : refs) {
                // Build filename: assetId_sourceName
                std::string safeName = ref.sourceName;
                for (char& c : safeName) {
                    if (!isalnum(c) && c != '-' && c != '_' && c != ' ') c = '_';
                }
                if (safeName.size() > 50) safeName = safeName.substr(0, 50);
                
                std::string filename = ref.assetId;
                if (!safeName.empty()) filename += "_" + safeName;
                
                fs::path outPath = categoryDir / filename;
                
                std::cout << "  [" << (downloaded + failed + 1) << "/" << totalAssets << "] "
                         << category << " " << ref.assetId << " (" << ref.sourceName << ")... ";
                
                if (DownloadAsset(ref.assetId, outPath)) {
                    downloaded++;
                    std::cout << "OK\n";
                } else {
                    failed++;
                    std::cout << "FAILED\n";
                }
            }
        }
        
        std::cout << "\n[*] Download complete: " << downloaded << " OK, " << failed << " failed, " << skipped << " skipped\n";
    } else if (anyDownload) {
        std::cout << "\n[*] No downloadable assets found in this game's instances.\n";
    }
    
    // Save asset manifest (JSON-like text file)
    if (totalAssets > 0) {
        fs::path manifestPath = baseDir / "assets_manifest.txt";
        std::ofstream manifest(manifestPath);
        if (manifest) {
            manifest << "Protoon Asset Manifest\n";
            manifest << "PlaceId: " << placeId << "\n";
            manifest << "GameId: " << gameId << "\n";
            manifest << "Instances: " << instances.size() << "\n";
            manifest << "Total Assets: " << totalAssets << "\n\n";
            
            for (const auto& [cat, refs] : assetsByCategory) {
                manifest << "=== " << cat << " (" << refs.size() << ") ===\n";
                for (const auto& ref : refs) {
                    manifest << "  ID: " << ref.assetId << "\n";
                    manifest << "  Source: " << ref.sourceName << " (" << ref.sourceClass << ")\n";
                    if (!ref.rawUrl.empty()) manifest << "  URL: " << ref.rawUrl << "\n";
                    manifest << "\n";
                }
            }
            
            manifest.close();
            std::cout << "[+] Asset manifest: " << manifestPath.string() << "\n";
        }
    }
    
    // Final summary
    auto endTime = std::chrono::steady_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();
    
    std::cout << "\n========================================\n";
    std::cout << "[+] EXTRACTION COMPLETE!\n";
    std::cout << "[+] Time: " << std::fixed << std::setprecision(1) << totalSeconds << "s\n";
    std::cout << "[+] Output: " << fs::absolute(baseDir).string() << "\n";
    
    // List output contents
    std::cout << "[+] Contents:\n";
    for (const auto& entry : fs::directory_iterator(baseDir)) {
        if (entry.is_directory()) {
            int count = 0;
            for (const auto& _ : fs::directory_iterator(entry.path())) count++;
            std::cout << "    " << entry.path().filename().string() << "/ (" << count << " files)\n";
        } else {
            auto size = entry.file_size();
            std::string sizeStr;
            if (size > 1048576) sizeStr = std::to_string(size / 1048576) + " MB";
            else if (size > 1024) sizeStr = std::to_string(size / 1024) + " KB";
            else sizeStr = std::to_string(size) + " B";
            std::cout << "    " << entry.path().filename().string() << " (" << sizeStr << ")\n";
        }
    }
    
    if (extractMap) {
        std::cout << "[+] Open Map.rbxlx in Roblox Studio to view the map!\n";
    }
    std::cout << "========================================\n";
    
    if (debugMode) {
        std::cout << "\n[DEBUG] Memory read stats: " << reader.GetTotalReads() << " total, " 
                 << reader.GetFailedReads() << " failed\n";
    }
    
    WaitForExit();
    return 0;
}
