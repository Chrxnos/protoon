import { useState, useEffect } from "react";
import "./App.css";
import { Download, Cpu, Shield, Zap, Github, ChevronDown, Terminal, Box, Layers, ExternalLink, X } from "lucide-react";
import axios from "axios";

const BACKEND_URL = process.env.REACT_APP_BACKEND_URL;
const API = `${BACKEND_URL}/api`;

const FeatureCard = ({ icon: Icon, title, description }) => (
  <div className="feature-card" data-testid={`feature-${title.toLowerCase().replace(/\s+/g, '-')}`}>
    <div className="feature-icon">
      <Icon size={32} />
    </div>
    <h3>{title}</h3>
    <p>{description}</p>
  </div>
);

const TechniqueSection = () => (
  <div className="technique-section" data-testid="technique-section">
    <h3>How Protoon Works</h3>
    <div className="technique-content">
      <div className="technique-problem">
        <h4>The Challenge</h4>
        <ul>
          <li>Roblox uses proprietary QUIC protocol (not RakNet) with encryption</li>
          <li>Hyperion anti-cheat blocks user-mode memory reading</li>
          <li>DLL hooking is detected and blocked</li>
        </ul>
      </div>
      <div className="technique-solution">
        <h4>Our Solution: Kernel-Level Access</h4>
        <ul>
          <li>Kernel driver operates <strong>beneath</strong> Hyperion in OS hierarchy</li>
          <li>Direct memory read via MmCopyVirtualMemory</li>
          <li>Uses current offsets from community research</li>
          <li>Extracts full DataModel including part positions, sizes, materials</li>
        </ul>
      </div>
    </div>
  </div>
);

const DownloadModal = ({ isOpen, onClose, downloadInfo }) => {
  if (!isOpen || !downloadInfo) return null;
  
  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-content" onClick={e => e.stopPropagation()} data-testid="download-modal">
        <button className="modal-close" onClick={onClose}><X size={24} /></button>
        <h2>Download Protoon</h2>
        <p className="modal-subtitle">Kernel-Level Roblox Map Extractor</p>
        
        <div className="modal-section">
          <h4>Package Contents:</h4>
          <ul>
            {downloadInfo.package_contents?.map((item, i) => (
              <li key={i}>{item}</li>
            ))}
          </ul>
        </div>
        
        <div className="modal-section">
          <h4>Installation:</h4>
          <ol>
            {downloadInfo.installation?.map((step, i) => (
              <li key={i}>{step}</li>
            ))}
          </ol>
        </div>
        
        <div className="modal-section">
          <h4>Usage:</h4>
          <ol>
            {downloadInfo.usage?.map((step, i) => (
              <li key={i}>{step}</li>
            ))}
          </ol>
        </div>
        
        <div className="modal-buttons">
          <a 
            href={downloadInfo.download_url} 
            className="primary-btn"
            target="_blank"
            rel="noopener noreferrer"
            data-testid="download-zip-btn"
          >
            <Download size={20} />
            Download ZIP
          </a>
          <a 
            href={downloadInfo.github_releases}
            className="secondary-btn"
            target="_blank"
            rel="noopener noreferrer"
          >
            <Github size={20} />
            GitHub Releases
          </a>
        </div>
        
        <p className="modal-note">
          Requires Windows 10/11 x64 with Administrator privileges
        </p>
      </div>
    </div>
  );
};

const ToolCard = ({ tool, onDownload }) => {
  const [showInstructions, setShowInstructions] = useState(false);
  
  return (
    <div className="tool-card" data-testid={`tool-${tool.name.toLowerCase()}`}>
      <div className="tool-header">
        <h3>{tool.name}</h3>
        <span className="tool-version">v{tool.version}</span>
      </div>
      <p className="tool-description">{tool.description}</p>
      <div className="tool-meta">
        <span className="tool-size">{tool.size}</span>
        <span className="tool-platform">{tool.platform}</span>
      </div>
      <button 
        className="download-btn"
        onClick={() => onDownload(tool)}
        data-testid={`download-${tool.name.toLowerCase()}`}
      >
        <Download size={18} />
        Download
      </button>
    </div>
  );
};

const DemoSection = () => {
  const [stats, setStats] = useState({
    instances: 0,
    isLoading: false,
    loaded: false
  });

  const loadDemo = async () => {
    setStats(s => ({ ...s, isLoading: true }));
    try {
      const response = await axios.post(`${API}/capture/demo`);
      setStats({
        instances: response.data.instances,
        isLoading: false,
        loaded: true
      });
    } catch (e) {
      console.error("Demo load error:", e);
      setStats(s => ({ ...s, isLoading: false }));
    }
  };

  const exportDemo = async () => {
    try {
      const response = await axios.post(`${API}/capture/export`, 
        { filename: "demo_map.rbxlx" },
        { responseType: 'blob' }
      );
      
      const url = window.URL.createObjectURL(new Blob([response.data]));
      const link = document.createElement('a');
      link.href = url;
      link.setAttribute('download', 'demo_map.rbxlx');
      document.body.appendChild(link);
      link.click();
      link.remove();
    } catch (e) {
      console.error("Export error:", e);
    }
  };

  return (
    <div className="demo-section" data-testid="demo-section">
      <h3>Try It Now</h3>
      <p>Test the export functionality with demo data</p>
      <div className="demo-controls">
        <button 
          className="demo-btn"
          onClick={loadDemo}
          disabled={stats.isLoading}
          data-testid="load-demo-btn"
        >
          {stats.isLoading ? "Loading..." : "Load Demo Data"}
        </button>
        {stats.loaded && (
          <>
            <span className="demo-stats">{stats.instances} instances loaded</span>
            <button 
              className="export-btn"
              onClick={exportDemo}
              data-testid="export-demo-btn"
            >
              Export as .rbxlx
            </button>
          </>
        )}
      </div>
    </div>
  );
};

const CodeBlock = ({ code }) => (
  <div className="code-block">
    <div className="code-header">
      <Terminal size={14} />
      <span>saveinstance.luau</span>
    </div>
    <pre><code>{code}</code></pre>
  </div>
);

function App() {
  const [tools, setTools] = useState([]);
  const [loading, setLoading] = useState(true);
  const [showDownloadModal, setShowDownloadModal] = useState(false);
  const [protoonDownloadInfo, setProtoonDownloadInfo] = useState(null);

  useEffect(() => {
    fetchTools();
  }, []);

  const fetchTools = async () => {
    try {
      const response = await axios.get(`${API}/tools`);
      setTools(response.data);
    } catch (e) {
      console.error("Failed to fetch tools:", e);
      // Fallback data
      setTools([
        {
          name: "Protoon",
          version: "1.0.0",
          description: "Kernel-level Roblox map extractor. Reads game memory beneath Hyperion anti-cheat using kernel driver. Extracts DataModel instances and exports to .rbxlx for Roblox Studio.",
          download_url: "/api/download/protoon",
          size: "~500 KB",
          platform: "Windows x64 (Kernel)"
        },
        {
          name: "Fleasion",
          version: "Latest",
          description: "HTTP proxy-based asset extraction tool. Intercepts Roblox HTTP traffic to capture and replace textures, audio, meshes, and animations.",
          download_url: "https://github.com/qrhrqiohj/Fleasion/releases",
          size: "~10 MB",
          platform: "Windows x64"
        },
        {
          name: "USSI Script",
          version: "Latest",
          description: "Universal Syn Save Instance - LUA script for saving entire game maps. Requires an executor to run inside Roblox.",
          download_url: "/api/download/ussi",
          size: "~300 KB",
          platform: "Roblox Executor"
        }
      ]);
    } finally {
      setLoading(false);
    }
  };

  const handleDownload = async (tool) => {
    if (tool.name === "Protoon") {
      // Fetch download info and show modal
      try {
        const response = await axios.get(`${API}/download/protoon`);
        setProtoonDownloadInfo(response.data);
        setShowDownloadModal(true);
      } catch (e) {
        // Fallback to direct GitHub
        window.open("https://github.com/Chrxnos/protoon/releases", "_blank");
      }
    } else if (tool.download_url.startsWith('http')) {
      window.open(tool.download_url, '_blank');
    } else {
      window.location.href = `${BACKEND_URL}${tool.download_url}`;
    }
  };

  const ussiCode = `local Params = {
  RepoURL = "https://raw.githubusercontent.com/luau/UniversalSynSaveInstance/main/",
  SSI = "saveinstance",
}
local synsaveinstance = loadstring(game:HttpGet(Params.RepoURL .. Params.SSI .. ".luau", true), Params.SSI)()
local Options = {} -- See documentation for options
synsaveinstance(Options)`;

  return (
    <div className="app" data-testid="app-container">
      {/* Hero Section */}
      <section className="hero" data-testid="hero-section">
        <div className="hero-bg">
          <div className="grid-overlay"></div>
          <div className="glow glow-1"></div>
          <div className="glow glow-2"></div>
        </div>
        <div className="hero-content">
          <h1 className="logo-text" data-testid="logo">PROTOON</h1>
          <p className="tagline">Roblox Asset & Map Extraction Suite</p>
          <p className="subtitle">
            Extract animations, textures, meshes, and save entire game maps
          </p>
          <div className="hero-buttons">
            <button 
              className="primary-btn"
              onClick={() => document.getElementById('downloads').scrollIntoView({ behavior: 'smooth' })}
              data-testid="download-hero-btn"
            >
              <Download size={20} />
              Download
            </button>
            <a 
              href="https://github.com/qrhrqiohj/Fleasion" 
              target="_blank" 
              rel="noopener noreferrer"
              className="secondary-btn"
              data-testid="github-btn"
            >
              <Github size={20} />
              View Source
            </a>
          </div>
          <div className="scroll-indicator">
            <ChevronDown size={32} />
          </div>
        </div>
      </section>

      {/* Features Section */}
      <section className="features" data-testid="features-section">
        <h2>Powerful Features</h2>
        <div className="features-grid">
          <FeatureCard
            icon={Box}
            title="Asset Extraction"
            description="Intercept and extract textures, audio, meshes, animations, and other assets from any Roblox game in real-time."
          />
          <FeatureCard
            icon={Layers}
            title="Map Saving"
            description="Kernel-level memory reading extracts full DataModel. Get part positions, sizes, materials - everything needed to rebuild maps."
          />
          <FeatureCard
            icon={Cpu}
            title="Beneath Hyperion"
            description="Kernel driver operates at ring 0, below Hyperion's user-mode detection. Undetected memory access."
          />
          <FeatureCard
            icon={Zap}
            title="Current Offsets"
            description="Auto-updated offsets from community research. Works with latest Roblox version (version-b130242ed064436f)."
          />
          <FeatureCard
            icon={Shield}
            title="No Executor Needed"
            description="Unlike USSI, Protoon doesn't require a Roblox executor. Pure external memory reading."
          />
          <FeatureCard
            icon={Terminal}
            title="Open Source"
            description="Full source code available. Kernel driver, memory reader, and RBXLX exporter included."
          />
        </div>
        <TechniqueSection />
      </section>

      {/* Downloads Section */}
      <section className="downloads" id="downloads" data-testid="downloads-section">
        <h2>Download Tools</h2>
        <p className="section-subtitle">Choose the tool that fits your needs</p>
        
        {loading ? (
          <div className="loading" data-testid="loading">Loading tools...</div>
        ) : (
          <div className="tools-grid">
            {tools.map((tool, index) => (
              <ToolCard key={index} tool={tool} onDownload={handleDownload} />
            ))}
          </div>
        )}
        
        <DemoSection />
      </section>

      {/* USSI Section */}
      <section className="ussi-section" data-testid="ussi-section">
        <h2>Quick Start with USSI</h2>
        <p className="section-subtitle">
          Copy this code into your executor to save any game map
        </p>
        <CodeBlock code={ussiCode} />
        <div className="ussi-warning">
          <Shield size={20} />
          <span>Requires a Roblox executor with loadstring support</span>
        </div>
      </section>

      {/* How It Works Section */}
      <section className="how-it-works" data-testid="how-it-works-section">
        <h2>How It Works</h2>
        <div className="steps">
          <div className="step">
            <div className="step-number">1</div>
            <h3>Install Driver</h3>
            <p>Enable test signing, install kernel driver with sc create</p>
          </div>
          <div className="step">
            <div className="step-number">2</div>
            <h3>Join Game</h3>
            <p>Launch Roblox and join any game you want to extract</p>
          </div>
          <div className="step">
            <div className="step-number">3</div>
            <h3>Run Protoon</h3>
            <p>Execute Protoon.exe as Administrator</p>
          </div>
          <div className="step">
            <div className="step-number">4</div>
            <h3>Export</h3>
            <p>Map automatically extracted to .rbxlx file</p>
          </div>
        </div>
      </section>

      {/* Footer */}
      <footer className="footer" data-testid="footer">
        <div className="footer-content">
          <div className="footer-brand">
            <h3>PROTOON</h3>
            <p>Roblox Asset & Map Extraction Suite</p>
          </div>
          <div className="footer-links">
            <h4>Resources</h4>
            <a href="https://github.com/qrhrqiohj/Fleasion" target="_blank" rel="noopener noreferrer">Fleasion GitHub</a>
            <a href="https://github.com/luau/UniversalSynSaveInstance" target="_blank" rel="noopener noreferrer">USSI GitHub</a>
            <a href="https://github.com/NtReadVirtualMemory/Roblox-Offsets-Website" target="_blank" rel="noopener noreferrer">Roblox Offsets</a>
          </div>
          <div className="footer-links">
            <h4>Community</h4>
            <a href="https://discord.gg/hXyhKehEZF" target="_blank" rel="noopener noreferrer">Fleasion Discord</a>
            <a href="https://discord.gg/wx4ThpAsmw" target="_blank" rel="noopener noreferrer">USSI Discord</a>
          </div>
        </div>
        <div className="footer-bottom">
          <p>Built for educational and personal use. Not affiliated with Roblox Corporation.</p>
        </div>
      </footer>
      
      {/* Download Modal */}
      <DownloadModal 
        isOpen={showDownloadModal}
        onClose={() => setShowDownloadModal(false)}
        downloadInfo={protoonDownloadInfo}
      />
    </div>
  );
}

export default App;
