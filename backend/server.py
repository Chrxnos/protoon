from fastapi import FastAPI, APIRouter, HTTPException, BackgroundTasks
from fastapi.responses import FileResponse, JSONResponse
from dotenv import load_dotenv
from starlette.middleware.cors import CORSMiddleware
from motor.motor_asyncio import AsyncIOMotorClient
import os
import logging
from pathlib import Path
from pydantic import BaseModel, Field, ConfigDict
from typing import List, Optional, Dict, Any
import uuid
from datetime import datetime, timezone
import tempfile
import shutil

ROOT_DIR = Path(__file__).parent
load_dotenv(ROOT_DIR / '.env')

# MongoDB connection
mongo_url = os.environ['MONGO_URL']
client = AsyncIOMotorClient(mongo_url)
db = client[os.environ['DB_NAME']]

# Create the main app
app = FastAPI(title="Protoon API", version="1.0.0")

# Create router with /api prefix
api_router = APIRouter(prefix="/api")

# Import Protoon modules
from protoon.packet_capture import PacketCapture, MockPacketCapture, CaptureStats
from protoon.datamodel import DataModel, Instance

# Global capture instance
active_capture: Optional[PacketCapture] = None

# Models
class StatusCheck(BaseModel):
    model_config = ConfigDict(extra="ignore")
    id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    client_name: str
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))

class CaptureStatus(BaseModel):
    is_running: bool
    packets_captured: int
    packets_parsed: int
    instances_created: int
    properties_set: int
    errors: int
    duration: float
    packets_per_second: float

class ExportRequest(BaseModel):
    filename: str = "map_export.rbxlx"

class ToolInfo(BaseModel):
    name: str
    version: str
    description: str
    download_url: str
    size: str
    platform: str

# Routes
@api_router.get("/")
async def root():
    return {
        "message": "Protoon API - Roblox Asset & Map Extraction Tool",
        "version": "1.0.0",
        "tools": ["Fleasion", "MapSaver", "USSI"]
    }

@api_router.get("/tools", response_model=List[ToolInfo])
async def get_tools():
    """Get list of available tools for download"""
    return [
        ToolInfo(
            name="Protoon",
            version="1.1.0",
            description="Kernel-level Roblox asset & map extractor. Fleasion-style scrape options: extract decals, audio, animations, meshes, sky textures + full map saving. Downloads organized by game into categorized folders.",
            download_url="/api/download/protoon",
            size="~600 KB",
            platform="Windows x64 (Kernel Driver)"
        ),
        ToolInfo(
            name="Fleasion",
            version="Latest",
            description="HTTP proxy-based asset extraction tool. Intercepts Roblox HTTP traffic to capture and replace textures, audio, meshes, and animations in real-time.",
            download_url="https://github.com/qrhrqiohj/Fleasion/releases",
            size="~10 MB",
            platform="Windows x64"
        ),
        ToolInfo(
            name="USSI Script",
            version="Latest",
            description="Universal Syn Save Instance - LUA script for saving entire game maps. Requires an executor to run inside Roblox.",
            download_url="/api/download/ussi",
            size="~300 KB",
            platform="Roblox Executor"
        )
    ]

@api_router.get("/capture/status", response_model=CaptureStatus)
async def get_capture_status():
    """Get current capture status"""
    global active_capture
    
    if active_capture is None:
        return CaptureStatus(
            is_running=False,
            packets_captured=0,
            packets_parsed=0,
            instances_created=0,
            properties_set=0,
            errors=0,
            duration=0,
            packets_per_second=0
        )
    
    stats = active_capture.stats
    return CaptureStatus(
        is_running=active_capture.running,
        packets_captured=stats.packets_captured,
        packets_parsed=stats.packets_parsed,
        instances_created=stats.instances_created,
        properties_set=stats.properties_set,
        errors=stats.errors,
        duration=stats.duration,
        packets_per_second=stats.packets_per_second
    )

@api_router.post("/capture/start")
async def start_capture():
    """Start packet capture"""
    global active_capture
    
    if active_capture and active_capture.running:
        raise HTTPException(status_code=400, detail="Capture already running")
    
    try:
        active_capture = PacketCapture()
        active_capture.start_capture()
        return {"status": "started", "message": "Packet capture started"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@api_router.post("/capture/stop")
async def stop_capture():
    """Stop packet capture"""
    global active_capture
    
    if active_capture is None or not active_capture.running:
        raise HTTPException(status_code=400, detail="No capture running")
    
    active_capture.stop_capture()
    return {
        "status": "stopped",
        "stats": {
            "packets_captured": active_capture.stats.packets_captured,
            "instances_created": active_capture.stats.instances_created
        }
    }

@api_router.post("/capture/demo")
async def load_demo_data():
    """Load demo data for testing"""
    global active_capture
    
    active_capture = MockPacketCapture()
    active_capture.load_demo_data()
    
    return {
        "status": "loaded",
        "instances": active_capture.stats.instances_created,
        "message": "Demo data loaded successfully"
    }

@api_router.get("/capture/instances")
async def get_captured_instances():
    """Get list of captured instances"""
    global active_capture
    
    if active_capture is None:
        return {"instances": [], "count": 0}
    
    instances = []
    for ref, inst in active_capture.data_model.instances_by_ref.items():
        instances.append({
            "ref": ref,
            "class_name": inst.class_name,
            "name": inst.name or inst.properties.get('Name', inst.class_name),
            "children_count": len(inst.children),
            "properties_count": len(inst.properties)
        })
    
    return {"instances": instances, "count": len(instances)}

@api_router.post("/capture/export")
async def export_map(request: ExportRequest):
    """Export captured data to RBXLX file"""
    global active_capture
    
    if active_capture is None:
        raise HTTPException(status_code=400, detail="No capture data available")
    
    if len(active_capture.data_model.instances_by_ref) == 0:
        raise HTTPException(status_code=400, detail="No instances captured")
    
    try:
        # Create temp file
        temp_dir = tempfile.mkdtemp()
        filepath = os.path.join(temp_dir, request.filename)
        
        active_capture.export_to_rbxlx(filepath)
        
        return FileResponse(
            path=filepath,
            filename=request.filename,
            media_type="application/xml",
            background=BackgroundTasks().add_task(lambda: shutil.rmtree(temp_dir, ignore_errors=True))
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@api_router.get("/download/ussi")
async def download_ussi():
    """Download USSI script"""
    ussi_path = ROOT_DIR.parent / "analysis" / "ussi" / "UniversalSynSaveInstance-main" / "saveinstance.luau"
    
    if not ussi_path.exists():
        # Return inline script content as fallback
        script_content = '''--[[
    Universal Syn Save Instance
    https://github.com/luau/UniversalSynSaveInstance
    
    Usage:
    local Params = {
        RepoURL = "https://raw.githubusercontent.com/luau/UniversalSynSaveInstance/main/",
        SSI = "saveinstance",
    }
    local synsaveinstance = loadstring(game:HttpGet(Params.RepoURL .. Params.SSI .. ".luau", true), Params.SSI)()
    local Options = {}
    synsaveinstance(Options)
]]

-- Redirect to GitHub for full script
print("Download full script from: https://raw.githubusercontent.com/luau/UniversalSynSaveInstance/main/saveinstance.luau")
'''
        return JSONResponse(content={
            "script": script_content,
            "download_url": "https://raw.githubusercontent.com/luau/UniversalSynSaveInstance/main/saveinstance.luau"
        })
    
    return FileResponse(
        path=str(ussi_path),
        filename="saveinstance.luau",
        media_type="text/plain"
    )

@api_router.get("/download/protoon")
async def download_protoon():
    """Get Protoon download info and links"""
    return JSONResponse(content={
        "message": "Protoon - Roblox Asset & Map Extraction Tool",
        "version": "1.1.0",
        "download_url": "https://github.com/Chrxnos/protoon/releases/latest/download/Protoon-v1.1.0-win64.zip",
        "github_releases": "https://github.com/Chrxnos/protoon/releases",
        "approach": "Kernel-level memory reading + Roblox CDN asset downloading",
        "package_contents": [
            "Protoon.exe - Asset & map extractor with interactive menu",
            "install.bat - One-click installer",
            "uninstall.bat - Clean uninstaller",
            "driver.c - Kernel driver source (for undetected mode)",
            "README.md - Full documentation"
        ],
        "extraction_options": [
            "Map only (.rbxlx)",
            "Decals / Images",
            "Audio / Sounds",
            "Animations",
            "Meshes (MeshPart textures)",
            "Sky Textures",
            "All Assets (no map)",
            "Everything (map + all assets)"
        ],
        "installation": [
            "Download and extract the ZIP",
            "Right-click install.bat -> Run as Administrator",
            "Join a Roblox game and wait for it to load",
            "Run C:\\Protoon\\Protoon.exe as Administrator",
            "Choose extraction options from the menu"
        ],
        "usage": [
            "Join a Roblox game",
            "Wait for the game to fully load",
            "Run Protoon.exe as Administrator",
            "Choose what to extract (1-8)",
            "Check the Downloads/ folder for output"
        ],
        "requirements": [
            "Windows 10/11 x64",
            "Administrator privileges",
            "Internet connection (for asset downloading)"
        ],
        "offsets_source": "https://github.com/NtReadVirtualMemory/Roblox-Offsets-Website",
        "current_roblox_version": "version-b130242ed064436f"
    })

@api_router.get("/download/source")
async def download_source():
    """Get Protoon source code files"""
    from fastapi.responses import FileResponse
    import zipfile
    import tempfile
    import shutil
    
    # Create zip of kernel source
    temp_dir = tempfile.mkdtemp()
    zip_path = os.path.join(temp_dir, "protoon_kernel_source.zip")
    
    kernel_dir = ROOT_DIR / "protoon_kernel"
    
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for file in ["driver.c", "memory_reader.hpp", "main.cpp", "CMakeLists.txt", "BUILD.md"]:
            filepath = kernel_dir / file
            if filepath.exists():
                zipf.write(filepath, file)
    
    return FileResponse(
        path=zip_path,
        filename="protoon_kernel_source.zip",
        media_type="application/zip"
    )

@api_router.get("/health")
async def health_check():
    return {"status": "healthy", "service": "protoon-api"}

# Include router
app.include_router(api_router)

# CORS
app.add_middleware(
    CORSMiddleware,
    allow_credentials=True,
    allow_origins=os.environ.get('CORS_ORIGINS', '*').split(','),
    allow_methods=["*"],
    allow_headers=["*"],
)

# Logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

@app.on_event("shutdown")
async def shutdown_db_client():
    global active_capture
    if active_capture and active_capture.running:
        active_capture.stop_capture()
    client.close()
