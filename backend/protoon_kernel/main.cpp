/*
 * Protoon Main Application
 * Windows console application for Roblox map extraction
 * 
 * Uses kernel driver for undetected memory access
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "memory_reader.hpp"

// Material name lookup
const char* GetMaterialName(uint8_t material) {
    switch (material) {
        case 256: return "Plastic";
        case 272: return "SmoothPlastic";
        case 288: return "Wood";
        case 304: return "WoodPlanks";
        case 512: return "Slate";
        case 528: return "Concrete";
        case 784: return "Granite";
        case 800: return "Brick";
        case 816: return "Pebble";
        case 832: return "Cobblestone";
        case 848: return "Rock";
        case 864: return "Sandstone";
        case 880: return "Basalt";
        case 896: return "Marble";
        case 1024: return "Metal";
        case 1040: return "CorrodedMetal";
        case 1056: return "DiamondPlate";
        case 1072: return "Foil";
        case 1280: return "Grass";
        case 1296: return "LeafyGrass";
        case 1312: return "Sand";
        case 1328: return "Fabric";
        case 1536: return "Snow";
        case 1552: return "Ice";
        case 1568: return "Glacier";
        case 1584: return "Glass";
        case 1792: return "ForceField";
        case 2048: return "Neon";
        default: return "Plastic";
    }
}

// Generate RBXLX XML for instances
std::string GenerateRBXLX(const std::vector<MemoryInstance>& instances) {
    std::ostringstream xml;
    
    xml << R"(<?xml version="1.0" encoding="utf-8"?>)" << "\n";
    xml << R"(<roblox xmlns:xmime="http://www.w3.org/2005/05/xmlmime" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.roblox.com/roblox.xsd" version="4">)" << "\n";
    xml << "  <External>null</External>\n";
    xml << "  <External>nil</External>\n";
    
    // Group instances by parent
    std::unordered_map<uintptr_t, std::vector<const MemoryInstance*>> childrenMap;
    for (const auto& inst : instances) {
        childrenMap[inst.parent].push_back(&inst);
    }
    
    // Find root instances (Workspace children)
    uintptr_t workspaceAddr = 0;
    for (const auto& inst : instances) {
        if (inst.className == "Workspace") {
            workspaceAddr = inst.address;
            break;
        }
    }
    
    // Recursive function to write instance
    std::function<void(const MemoryInstance*, int)> writeInstance = [&](const MemoryInstance* inst, int indent) {
        std::string tabs(indent * 2, ' ');
        
        xml << tabs << "<Item class=\"" << inst->className << "\" referent=\"RBX" << std::hex << inst->address << std::dec << "\">\n";
        xml << tabs << "  <Properties>\n";
        
        // Name
        xml << tabs << "    <string name=\"Name\">" << inst->name << "</string>\n";
        
        // Part-specific properties
        if (inst->className == "Part" || inst->className == "MeshPart" || 
            inst->className == "WedgePart" || inst->className == "SpawnLocation") {
            
            // CFrame (simplified - just position)
            xml << tabs << "    <CoordinateFrame name=\"CFrame\">\n";
            xml << tabs << "      <X>" << inst->position[0] << "</X>\n";
            xml << tabs << "      <Y>" << inst->position[1] << "</Y>\n";
            xml << tabs << "      <Z>" << inst->position[2] << "</Z>\n";
            xml << tabs << "      <R00>1</R00><R01>0</R01><R02>0</R02>\n";
            xml << tabs << "      <R10>0</R10><R11>1</R11><R12>0</R12>\n";
            xml << tabs << "      <R20>0</R20><R21>0</R21><R22>1</R22>\n";
            xml << tabs << "    </CoordinateFrame>\n";
            
            // Size
            xml << tabs << "    <Vector3 name=\"size\">\n";
            xml << tabs << "      <X>" << inst->size[0] << "</X>\n";
            xml << tabs << "      <Y>" << inst->size[1] << "</Y>\n";
            xml << tabs << "      <Z>" << inst->size[2] << "</Z>\n";
            xml << tabs << "    </Vector3>\n";
            
            // Anchored
            xml << tabs << "    <bool name=\"Anchored\">" << (inst->anchored ? "true" : "false") << "</bool>\n";
            
            // CanCollide
            xml << tabs << "    <bool name=\"CanCollide\">" << (inst->canCollide ? "true" : "false") << "</bool>\n";
            
            // Transparency
            xml << tabs << "    <float name=\"Transparency\">" << inst->transparency << "</float>\n";
            
            // Material
            xml << tabs << "    <token name=\"Material\">" << GetMaterialName(inst->material) << "</token>\n";
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
    
    // Write Workspace and its children
    if (workspaceAddr) {
        auto it = childrenMap.find(workspaceAddr);
        if (it != childrenMap.end()) {
            xml << "  <Item class=\"Workspace\" referent=\"RBX" << std::hex << workspaceAddr << std::dec << "\">\n";
            xml << "    <Properties>\n";
            xml << "      <string name=\"Name\">Workspace</string>\n";
            xml << "    </Properties>\n";
            
            for (const auto* child : it->second) {
                writeInstance(child, 2);
            }
            
            xml << "  </Item>\n";
        }
    }
    
    xml << "</roblox>\n";
    return xml.str();
}

void PrintBanner() {
    std::cout << R"(
 ____            _                   
|  _ \ _ __ ___ | |_ ___   ___  _ __  
| |_) | '__/ _ \| __/ _ \ / _ \| '_ \ 
|  __/| | | (_) | || (_) | (_) | | | |
|_|   |_|  \___/ \__\___/ \___/|_| |_|
                                      
    Roblox Map Extraction Tool
    Kernel-Level Memory Reader
    )" << std::endl;
}

void WaitForExit() {
    std::cout << "\nPress Enter to exit...";
    std::cin.get();
}

int main(int argc, char* argv[]) {
    PrintBanner();
    
    std::string outputFile = "extracted_map.rbxlx";
    if (argc > 1) {
        outputFile = argv[1];
    }
    
    std::cout << "[*] Initializing Protoon...\n";
    
    ProtoonMemoryReader reader;
    
    if (!reader.Initialize()) {
        std::cout << "\n[!] Failed to initialize. Make sure:\n";
        std::cout << "    1. Roblox is running (RobloxPlayerBeta.exe)\n";
        std::cout << "    2. You are IN A GAME (not just the launcher)\n";
        std::cout << "    3. You're running as Administrator\n";
        std::cout << "    4. (Optional) Kernel driver is loaded for undetected mode\n";
        std::cout << "\nTo load driver:\n";
        std::cout << "    bcdedit /set testsigning on\n";
        std::cout << "    (reboot)\n";
        std::cout << "    sc create ProtoonDrv type= kernel binpath= \"C:\\Protoon\\ProtoonDriver.sys\"\n";
        std::cout << "    sc start ProtoonDrv\n";
        WaitForExit();
        return 1;
    }
    
    std::cout << "[*] Connected to Roblox (PID: " << reader.GetPid() << ")\n";
    std::cout << "[*] DataModel found at: 0x" << std::hex << reader.GetDataModel() << std::dec << "\n";
    std::cout << "[*] Extracting map data...\n";
    
    auto instances = reader.ExtractMap();
    
    if (instances.empty()) {
        std::cout << "[!] No instances extracted.\n";
        std::cout << "    Make sure you're inside a Roblox game (not the menu).\n";
        WaitForExit();
        return 1;
    }
    
    std::cout << "[*] Extracted " << instances.size() << " instances\n";
    
    // Count parts
    int partCount = 0;
    for (const auto& inst : instances) {
        if (inst.className == "Part" || inst.className == "MeshPart" || 
            inst.className == "WedgePart") {
            partCount++;
        }
    }
    std::cout << "[*] Found " << partCount << " parts\n";
    
    // Generate and save RBXLX
    std::cout << "[*] Generating RBXLX file...\n";
    std::string xml = GenerateRBXLX(instances);
    
    std::ofstream file(outputFile);
    if (!file) {
        std::cout << "[!] Failed to create output file\n";
        WaitForExit();
        return 1;
    }
    
    file << xml;
    file.close();
    
    std::cout << "\n========================================\n";
    std::cout << "[+] SUCCESS! Map saved to: " << outputFile << "\n";
    std::cout << "[+] You can now open this file in Roblox Studio!\n";
    std::cout << "========================================\n";
    
    WaitForExit();
    return 0;
}
