/*
 * Protoon v1.4.0 - Roblox Asset & Map Extraction Tool
 * 
 * Features:
 *   - Full game instance tree extraction (75k+ instances)
 *   - Authenticated asset downloading via Roblox CDN
 *   - Organized Downloads folder per game with categories
 *   - Fleasion-style scrape options (8 modes)
 *   - Kernel driver for undetected mode (optional)
 *   - Debug mode for troubleshooting
 *   - Retry logic + progress tracking for downloads
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
#include <thread>

#include "memory_reader.hpp"

// For HTTP downloads
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// For SHGetFolderPath + GetModuleFileName
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

// ============================================================
// Get executable directory (fixes path duplication bug)
// Uses wide API for full Unicode path support on Windows
// ============================================================
fs::path GetExeDirectory() {
    wchar_t exePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        fs::path p = fs::path(exePath).parent_path();
        // Ensure it's an absolute path
        if (p.is_absolute()) return p;
    }
    // Fallback: current working directory
    return fs::current_path();
}

// ============================================================
// Read .ROBLOSECURITY cookie for authenticated CDN downloads
// ============================================================
std::string g_robloxCookie;

std::string LoadCookie(const fs::path& exeDir) {
    // Check cookie.txt in exe directory
    fs::path cookiePath = exeDir / "cookie.txt";
    if (fs::exists(cookiePath)) {
        std::ifstream f(cookiePath);
        std::string cookie;
        std::getline(f, cookie);
        // Trim whitespace
        while (!cookie.empty() && (cookie.back() == '\n' || cookie.back() == '\r' || cookie.back() == ' '))
            cookie.pop_back();
        if (!cookie.empty()) {
            printf("[+] Loaded auth cookie from cookie.txt\n");
            return cookie;
        }
    }
    return "";
}

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
// Detect file extension from binary content
// ============================================================
std::string DetectFileExtension(const std::vector<BYTE>& data) {
    if (data.size() < 4) return ".bin";
    
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return ".png";
    if (data[0] == 0xFF && data[1] == 0xD8) return ".jpg";
    if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') return ".gif";
    if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
        && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') return ".webp";
    if (data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S') return ".ogg";
    if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') return ".mp3";
    if (data[0] == 0xFF && (data[1] == 0xFB || data[1] == 0xF3 || data[1] == 0xF2)) return ".mp3";
    if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') return ".wav";
    if (data.size() >= 8 && memcmp(data.data(), "version ", 8) == 0) return ".mesh";
    if (data[0] == '<') return ".xml";
    if (data[0] == 0xAB && data[1] == 'K' && data[2] == 'T' && data[3] == 'X') return ".ktx";
    if (data[0] == '{') return ".json";
    
    return ".bin";
}

// ============================================================
// HTTP Download using WinHTTP (with auth + retry + redirect)
// ============================================================
struct DownloadResult {
    bool success = false;
    int httpStatus = 0;
    size_t bytesDownloaded = 0;
    std::string error;
};

DownloadResult HttpGet(const std::wstring& host, const std::wstring& path,
                       const std::string& cookie, std::vector<BYTE>& outData) {
    DownloadResult result;
    
    HINTERNET hSession = WinHttpOpen(L"Protoon/1.4",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { result.error = "WinHttpOpen failed"; return result; }
    
    WinHttpSetTimeouts(hSession, 5000, 15000, 15000, 60000);
    
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        result.error = "WinHttpConnect failed";
        return result;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        result.error = "WinHttpOpenRequest failed";
        return result;
    }
    
    // Enable auto-redirect
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    
    // Build headers (auth cookie if available)
    std::wstring headers;
    if (!cookie.empty()) {
        std::wstring wCookie(cookie.begin(), cookie.end());
        headers = L"Cookie: .ROBLOSECURITY=" + wCookie + L"\r\n";
    }
    
    BOOL sent = WinHttpSendRequest(hRequest,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : (DWORD)headers.size(),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    
    if (!sent) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        result.error = "WinHttpSendRequest failed";
        return result;
    }
    
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        result.error = "WinHttpReceiveResponse failed";
        return result;
    }
    
    // Check status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.httpStatus = (int)statusCode;
    
    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        result.error = "HTTP " + std::to_string(statusCode);
        return result;
    }
    
    // Read response body
    DWORD bytesRead;
    BYTE buffer[8192];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        outData.insert(outData.end(), buffer, buffer + bytesRead);
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    result.bytesDownloaded = outData.size();
    result.success = !outData.empty();
    return result;
}

bool DownloadAsset(const std::string& assetId, const fs::path& outputPath, bool debugMode = false) {
    std::vector<BYTE> data;
    DownloadResult result;
    
    // Try 1: New OpenCloud endpoint (apis.roblox.com) with auth
    if (!g_robloxCookie.empty()) {
        std::wstring wAssetId(assetId.begin(), assetId.end());
        result = HttpGet(L"apis.roblox.com",
            L"/asset-delivery-api/v1/assetId/" + wAssetId,
            g_robloxCookie, data);
        
        if (result.success) goto save_file;
        if (debugMode) printf("[DEBUG] OpenCloud endpoint: %s\n", result.error.c_str());
        data.clear();
    }
    
    // Try 2: Legacy endpoint with auth cookie
    {
        std::wstring wAssetId(assetId.begin(), assetId.end());
        result = HttpGet(L"assetdelivery.roblox.com",
            L"/v1/asset/?id=" + wAssetId,
            g_robloxCookie, data);
        
        if (result.success) goto save_file;
        if (debugMode) printf("[DEBUG] Legacy endpoint: %s\n", result.error.c_str());
        data.clear();
    }
    
    // Try 3: Retry legacy without cookie (some public assets may still work)
    if (!g_robloxCookie.empty()) {
        std::wstring wAssetId(assetId.begin(), assetId.end());
        result = HttpGet(L"assetdelivery.roblox.com",
            L"/v1/asset/?id=" + wAssetId,
            "", data);
        
        if (result.success) goto save_file;
        data.clear();
    }
    
    return false;

save_file:
    if (data.empty()) return false;
    
    // Check if response is a JSON redirect (CDN returns location URL)
    if (data.size() < 4096 && data[0] == '{') {
        std::string jsonStr(data.begin(), data.end());
        // Look for "location" field in JSON response
        size_t locPos = jsonStr.find("\"location\"");
        if (locPos == std::string::npos) locPos = jsonStr.find("\"Location\"");
        if (locPos != std::string::npos) {
            size_t urlStart = jsonStr.find("\"http", locPos + 8);
            if (urlStart != std::string::npos) {
                urlStart++; // skip opening quote
                size_t urlEnd = jsonStr.find("\"", urlStart);
                if (urlEnd != std::string::npos) {
                    std::string cdnUrl = jsonStr.substr(urlStart, urlEnd - urlStart);
                    
                    // Parse the CDN URL and download from it
                    // Extract host and path from full URL
                    size_t hostStart = cdnUrl.find("://") + 3;
                    size_t pathStart = cdnUrl.find("/", hostStart);
                    if (hostStart > 3 && pathStart != std::string::npos) {
                        std::string cdnHost = cdnUrl.substr(hostStart, pathStart - hostStart);
                        std::string cdnPath = cdnUrl.substr(pathStart);
                        std::wstring wHost(cdnHost.begin(), cdnHost.end());
                        std::wstring wPath(cdnPath.begin(), cdnPath.end());
                        
                        data.clear();
                        result = HttpGet(wHost, wPath, "", data);
                        if (!result.success || data.empty()) return false;
                    }
                }
            }
        }
    }
    
    // Detect file extension from content
    std::string ext = DetectFileExtension(data);
    
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
// RBXLX Generation - Workspace-only with visual filtering
// ============================================================
std::string GenerateRBXLX(const std::vector<MemoryInstance>& instances) {
    std::ostringstream xml;
    
    xml << R"(<?xml version="1.0" encoding="utf-8"?>)" << "\n";
    xml << R"(<roblox xmlns:xmime="http://www.w3.org/2005/05/xmlmime" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.roblox.com/roblox.xsd" version="4">)" << "\n";
    xml << "  <External>null</External>\n";
    xml << "  <External>nil</External>\n";
    
    // Build lookup maps
    std::unordered_map<uintptr_t, std::vector<const MemoryInstance*>> childrenMap;
    std::unordered_map<uintptr_t, const MemoryInstance*> instanceMap;
    
    for (const auto& inst : instances) {
        instanceMap[inst.address] = &inst;
        childrenMap[inst.parent].push_back(&inst);
    }
    
    // Find the Workspace instance
    const MemoryInstance* workspace = nullptr;
    for (const auto& inst : instances) {
        if (inst.className == "Workspace") {
            workspace = &inst;
            break;
        }
    }
    
    if (!workspace) {
        xml << "</roblox>\n";
        return xml.str();
    }
    
    // Classes to SKIP in the RBXLX (non-visual or singleton services that cause errors)
    static const std::set<std::string> skipClasses = {
        "Terrain",  // Workspace already has Terrain — adding another causes load error
        "ModuleScript", "LocalScript", "Script",
        "RemoteEvent", "RemoteFunction", "BindableEvent", "BindableFunction",
        "ImageLabel", "ImageButton", "TextLabel", "TextButton", "TextBox",
        "Frame", "ScrollingFrame", "ViewportFrame",
        "UIGradient", "UIStroke", "UICorner", "UIAspectRatioConstraint",
        "UIListLayout", "UIGridLayout", "UITableLayout", "UIPadding", "UIScale", "UISizeConstraint",
        "UITextSizeConstraint", "UIPageLayout",
        "ScreenGui", "SurfaceGui", "BillboardGui", "PlayerGui",
        "StatsItem", "NumberValue", "StringValue", "BoolValue", "IntValue",
        "ObjectValue", "CFrameValue", "Color3Value", "Vector3Value",
        "BrickColorValue", "RayValue",
        "Configuration", "Camera"
    };
    
    // Part-like classes that get CFrame/Size/Color properties
    static const std::set<std::string> partClasses = {
        "Part", "MeshPart", "WedgePart", "SpawnLocation", "TrussPart",
        "CornerWedgePart", "UnionOperation", "NegateOperation", "Seat", "VehicleSeat"
    };
    
    // Recursive writer - only writes non-skipped classes
    int refCounter = 1;
    int writtenParts = 0;
    
    std::function<void(const MemoryInstance*, int)> writeInstance = [&](const MemoryInstance* inst, int indent) {
        // Skip non-visual classes
        if (skipClasses.count(inst->className)) return;
        
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
            writtenParts++;
            
            // CFrame
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
        
        // Write children (only non-skipped ones)
        auto it = childrenMap.find(inst->address);
        if (it != childrenMap.end()) {
            for (const auto* child : it->second) {
                writeInstance(child, indent + 1);
            }
        }
        
        xml << tabs << "</Item>\n";
    };
    
    // Write only Workspace children (not the Workspace itself, not other services)
    auto wsChildren = childrenMap.find(workspace->address);
    if (wsChildren != childrenMap.end()) {
        for (const auto* child : wsChildren->second) {
            writeInstance(child, 1);
        }
    }
    
    printf("[+] RBXLX: %d visual parts written (skipped scripts/UI)\n", writtenParts);
    
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
    v1.4.0 - Authenticated CDN Downloads
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
    std::cout << "  --debug        Enable debug mode (verbose output)\n";
    std::cout << "  --output DIR   Set output directory (default: ./Downloads)\n";
    std::cout << "  --cookie COOK  Set .ROBLOSECURITY cookie for authenticated CDN downloads\n";
    std::cout << "                 (or place cookie in cookie.txt next to Protoon.exe)\n";
    std::cout << "  --help         Show this help message\n";
    std::cout << std::endl;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    PrintBanner();
    
    // Get exe directory (fixes path duplication when CWD != exe dir)
    fs::path exeDir = GetExeDirectory();
    
    // Parse arguments
    bool debugMode = false;
    std::string outputDir = "";
    std::string cookieArg = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--debug") debugMode = true;
        else if (arg == "--quiet") debugMode = false;
        else if (arg == "--output" && i + 1 < argc) outputDir = argv[++i];
        else if (arg == "--cookie" && i + 1 < argc) cookieArg = argv[++i];
        else if (arg == "--help") { PrintHelp(); return 0; }
    }
    
    // Load auth cookie: --cookie flag > cookie.txt > empty
    if (!cookieArg.empty()) {
        g_robloxCookie = cookieArg;
        printf("[+] Using cookie from --cookie argument\n");
    } else {
        g_robloxCookie = LoadCookie(exeDir);
    }
    
    if (g_robloxCookie.empty()) {
        printf("[*] No auth cookie found. Asset downloads may fail (Roblox requires auth since 2025).\n");
        printf("[*] To fix: place your .ROBLOSECURITY cookie in cookie.txt next to Protoon.exe\n");
        printf("[*]   or use: Protoon.exe --cookie YOUR_COOKIE_HERE\n\n");
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
    
    // Create output directory — always resolve relative to exe directory
    // This fixes the path duplication bug (Downloads/Game/Downloads/Game)
    // when CWD differs from exe location
    std::string gameFolderName = "Game_" + std::to_string(placeId);
    
    fs::path baseDir;
    if (outputDir.empty()) {
        // Default: create Downloads/ next to the exe
        baseDir = exeDir / "Downloads" / gameFolderName;
    } else {
        fs::path outPath(outputDir);
        if (outPath.is_absolute()) {
            baseDir = outPath / gameFolderName;
        } else {
            // Relative paths resolve from exe directory, not CWD
            baseDir = exeDir / outPath / gameFolderName;
        }
    }
    
    // Safety: detect and fix path duplication
    // e.g. .../Downloads/Game_123/Downloads/Game_123 → .../Downloads/Game_123
    {
        std::string pathStr = baseDir.string();
        std::string segment = "Downloads" + std::string(1, fs::path::preferred_separator) + gameFolderName;
        size_t firstOccurrence = pathStr.find(segment);
        if (firstOccurrence != std::string::npos) {
            size_t secondOccurrence = pathStr.find(segment, firstOccurrence + segment.size());
            if (secondOccurrence != std::string::npos) {
                // Duplicated — trim to first occurrence only
                baseDir = fs::path(pathStr.substr(0, firstOccurrence + segment.size()));
                printf("[*] Fixed duplicated path segment\n");
            }
        }
    }
    
    // Print resolved path for diagnostics
    printf("[*] Exe directory: %s\n", exeDir.string().c_str());
    
    fs::create_directories(baseDir);
    
    if (extractDecals) fs::create_directories(baseDir / "Decals");
    if (extractAudio) fs::create_directories(baseDir / "Audio");
    if (extractAnimations) fs::create_directories(baseDir / "Animations");
    if (extractMeshes) fs::create_directories(baseDir / "Meshes");
    if (extractSky) fs::create_directories(baseDir / "Sky");
    
    std::cout << "\n[*] Output directory: " << baseDir.string() << "\n";
    
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
    
    // Only collect asset references if user selected an asset extraction option
    bool anyDownload = extractDecals || extractAudio || extractAnimations || extractMeshes || extractSky;
    
    for (const auto& inst : instances) {
        classCounts[inst.className]++;
        
        if (inst.className == "Part" || inst.className == "MeshPart" || 
            inst.className == "WedgePart" || inst.className == "UnionOperation") {
            partCount++;
        }
        
        // Only collect asset references if user wants assets
        if (anyDownload) {
            for (const auto& ref : inst.assetRefs) {
                allAssets.push_back(ref);
            }
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
    
    // Deduplicate assets (only if user requested asset extraction)
    std::set<std::string> seenAssets;
    std::map<std::string, std::vector<AssetReference>> assetsByCategory;
    int totalAssets = 0;
    
    if (anyDownload) {
        for (const auto& ref : allAssets) {
            if (seenAssets.count(ref.assetId)) continue;
            seenAssets.insert(ref.assetId);
            assetsByCategory[ref.category].push_back(ref);
        }
        
        // Show asset summary
        std::cout << "\n[*] Asset references found:\n";
        for (const auto& [cat, refs] : assetsByCategory) {
            std::cout << "    " << std::setw(15) << std::left << cat << " " << refs.size() << " unique\n";
            totalAssets += (int)refs.size();
        }
        std::cout << "    " << std::setw(15) << std::left << "TOTAL" << " " << totalAssets << " unique assets\n";
    }
    
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
                
                if (DownloadAsset(ref.assetId, outPath, debugMode)) {
                    downloaded++;
                    std::cout << "OK\n";
                } else {
                    // Retry once after short delay
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (DownloadAsset(ref.assetId, outPath, debugMode)) {
                        downloaded++;
                        std::cout << "OK (retry)\n";
                    } else {
                        failed++;
                        std::cout << "FAILED\n";
                    }
                }
            }
        }
        
        std::cout << "\n[*] Download complete: " << downloaded << " OK, " << failed << " failed, " << skipped << " skipped\n";
    } else if (anyDownload) {
        std::cout << "\n[*] No downloadable assets found in this game's instances.\n";
    }
    
    // Save asset manifest (only when assets were requested and found)
    if (anyDownload && totalAssets > 0) {
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
    std::cout << "[+] Output: " << baseDir.string() << "\n";
    
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
