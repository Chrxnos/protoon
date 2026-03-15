"""
Protoon GUI - Desktop application for Roblox map extraction
Built with PyQt6 for Windows
"""
import sys
import os
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QTextEdit, QProgressBar, QFileDialog,
    QMessageBox, QTabWidget, QGroupBox, QListWidget, QListWidgetItem,
    QSplitter, QStatusBar, QMenuBar, QMenu, QSystemTrayIcon
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QThread
from PyQt6.QtGui import QIcon, QAction, QFont, QColor

from protoon.packet_capture import PacketCapture, MockPacketCapture
from protoon.datamodel import Instance

class CaptureThread(QThread):
    """Background thread for packet capture"""
    instance_created = pyqtSignal(str, str)  # ref, class_name
    packet_received = pyqtSignal(str)  # packet info
    error_occurred = pyqtSignal(str)
    
    def __init__(self, capture: PacketCapture):
        super().__init__()
        self.capture = capture
        self.capture.on_instance_created = self._on_instance
        self.capture.on_packet_received = self._on_packet
    
    def _on_instance(self, instance: Instance):
        self.instance_created.emit(instance.ref, instance.class_name)
    
    def _on_packet(self, data: bytes, name: str):
        self.packet_received.emit(f"{name} ({len(data)} bytes)")
    
    def run(self):
        try:
            self.capture.start_capture()
            while self.capture.running:
                self.msleep(100)
        except Exception as e:
            self.error_occurred.emit(str(e))

class ProtoonWindow(QMainWindow):
    """Main application window"""
    
    def __init__(self):
        super().__init__()
        self.capture: PacketCapture = None
        self.capture_thread: CaptureThread = None
        
        self.setWindowTitle("Protoon - Roblox Map Saver")
        self.setMinimumSize(900, 600)
        
        self._setup_ui()
        self._setup_menu()
        self._setup_statusbar()
        self._setup_timer()
        
        # Apply dark theme
        self._apply_dark_theme()
    
    def _setup_ui(self):
        """Setup main UI components"""
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        
        # Header
        header = QLabel("PROTOON")
        header.setFont(QFont("Segoe UI", 24, QFont.Weight.Bold))
        header.setAlignment(Qt.AlignmentFlag.AlignCenter)
        header.setStyleSheet("color: #00ff88; margin: 10px;")
        layout.addWidget(header)
        
        subtitle = QLabel("Roblox Map & Asset Extraction Tool")
        subtitle.setAlignment(Qt.AlignmentFlag.AlignCenter)
        subtitle.setStyleSheet("color: #888; margin-bottom: 10px;")
        layout.addWidget(subtitle)
        
        # Tabs
        tabs = QTabWidget()
        layout.addWidget(tabs)
        
        # Capture Tab
        capture_tab = self._create_capture_tab()
        tabs.addTab(capture_tab, "Capture")
        
        # Instances Tab
        instances_tab = self._create_instances_tab()
        tabs.addTab(instances_tab, "Instances")
        
        # Log Tab
        log_tab = self._create_log_tab()
        tabs.addTab(log_tab, "Log")
        
        # About Tab
        about_tab = self._create_about_tab()
        tabs.addTab(about_tab, "About")
    
    def _create_capture_tab(self) -> QWidget:
        """Create capture control tab"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        # Control buttons
        btn_layout = QHBoxLayout()
        
        self.start_btn = QPushButton("Start Capture")
        self.start_btn.clicked.connect(self._start_capture)
        self.start_btn.setStyleSheet("""
            QPushButton {
                background-color: #00aa55;
                color: white;
                padding: 15px 30px;
                font-size: 14px;
                font-weight: bold;
                border-radius: 5px;
            }
            QPushButton:hover { background-color: #00cc66; }
            QPushButton:disabled { background-color: #444; }
        """)
        btn_layout.addWidget(self.start_btn)
        
        self.stop_btn = QPushButton("Stop Capture")
        self.stop_btn.clicked.connect(self._stop_capture)
        self.stop_btn.setEnabled(False)
        self.stop_btn.setStyleSheet("""
            QPushButton {
                background-color: #aa3333;
                color: white;
                padding: 15px 30px;
                font-size: 14px;
                font-weight: bold;
                border-radius: 5px;
            }
            QPushButton:hover { background-color: #cc4444; }
            QPushButton:disabled { background-color: #444; }
        """)
        btn_layout.addWidget(self.stop_btn)
        
        self.export_btn = QPushButton("Export Map")
        self.export_btn.clicked.connect(self._export_map)
        self.export_btn.setStyleSheet("""
            QPushButton {
                background-color: #3366aa;
                color: white;
                padding: 15px 30px;
                font-size: 14px;
                font-weight: bold;
                border-radius: 5px;
            }
            QPushButton:hover { background-color: #4477bb; }
        """)
        btn_layout.addWidget(self.export_btn)
        
        demo_btn = QPushButton("Load Demo")
        demo_btn.clicked.connect(self._load_demo)
        demo_btn.setStyleSheet("""
            QPushButton {
                background-color: #666;
                color: white;
                padding: 15px 20px;
                font-size: 12px;
                border-radius: 5px;
            }
            QPushButton:hover { background-color: #777; }
        """)
        btn_layout.addWidget(demo_btn)
        
        layout.addLayout(btn_layout)
        
        # Stats group
        stats_group = QGroupBox("Capture Statistics")
        stats_layout = QVBoxLayout(stats_group)
        
        self.stats_labels = {}
        for stat in ['Packets Captured', 'Packets Parsed', 'Instances Created', 'Properties Set', 'Errors', 'Duration']:
            row = QHBoxLayout()
            label = QLabel(f"{stat}:")
            label.setStyleSheet("color: #888;")
            value = QLabel("0")
            value.setStyleSheet("color: #00ff88; font-weight: bold;")
            self.stats_labels[stat] = value
            row.addWidget(label)
            row.addStretch()
            row.addWidget(value)
            stats_layout.addLayout(row)
        
        layout.addWidget(stats_group)
        
        # Progress bar
        self.progress = QProgressBar()
        self.progress.setRange(0, 0)  # Indeterminate
        self.progress.setVisible(False)
        layout.addWidget(self.progress)
        
        # Instructions
        instructions = QLabel("""
        <b>How to use:</b><br>
        1. Run Protoon as Administrator<br>
        2. Click "Start Capture"<br>
        3. Launch Roblox and join a game<br>
        4. Play/walk around to load map data<br>
        5. Click "Stop Capture" and "Export Map"
        """)
        instructions.setStyleSheet("color: #888; padding: 10px; background: #1a1a2e; border-radius: 5px;")
        layout.addWidget(instructions)
        
        layout.addStretch()
        return widget
    
    def _create_instances_tab(self) -> QWidget:
        """Create instances browser tab"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        self.instance_list = QListWidget()
        self.instance_list.setStyleSheet("""
            QListWidget {
                background-color: #1a1a2e;
                color: #fff;
                border: 1px solid #333;
            }
            QListWidget::item:selected {
                background-color: #00aa55;
            }
        """)
        layout.addWidget(self.instance_list)
        
        count_label = QLabel("Captured Instances: 0")
        count_label.setStyleSheet("color: #888;")
        self.instance_count_label = count_label
        layout.addWidget(count_label)
        
        return widget
    
    def _create_log_tab(self) -> QWidget:
        """Create log viewer tab"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setStyleSheet("""
            QTextEdit {
                background-color: #0a0a1a;
                color: #00ff88;
                font-family: 'Consolas', monospace;
                border: 1px solid #333;
            }
        """)
        layout.addWidget(self.log_text)
        
        clear_btn = QPushButton("Clear Log")
        clear_btn.clicked.connect(lambda: self.log_text.clear())
        layout.addWidget(clear_btn)
        
        return widget
    
    def _create_about_tab(self) -> QWidget:
        """Create about tab"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        about_text = QLabel("""
        <h2 style="color: #00ff88;">Protoon v1.0.0</h2>
        <p>Roblox Map & Asset Extraction Tool</p>
        <br>
        <p><b>Features:</b></p>
        <ul>
            <li>UDP packet capture for map data extraction</li>
            <li>Instance hierarchy reconstruction</li>
            <li>Export to RBXLX format</li>
            <li>Based on RakNet protocol analysis</li>
        </ul>
        <br>
        <p><b>Credits:</b></p>
        <ul>
            <li>roblox-dissector by Gskartwii</li>
            <li>Fleasion for asset extraction</li>
            <li>UniversalSynSaveInstance for LUA saving</li>
        </ul>
        <br>
        <p style="color: #888;">
        This tool intercepts network packets to capture game instances.<br>
        Run as Administrator for packet capture to work.
        </p>
        """)
        about_text.setStyleSheet("color: #fff; padding: 20px;")
        about_text.setWordWrap(True)
        layout.addWidget(about_text)
        layout.addStretch()
        
        return widget
    
    def _setup_menu(self):
        """Setup menu bar"""
        menubar = self.menuBar()
        
        # File menu
        file_menu = menubar.addMenu("File")
        
        export_action = QAction("Export Map...", self)
        export_action.triggered.connect(self._export_map)
        file_menu.addAction(export_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("Exit", self)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # Help menu
        help_menu = menubar.addMenu("Help")
        
        about_action = QAction("About", self)
        help_menu.addAction(about_action)
    
    def _setup_statusbar(self):
        """Setup status bar"""
        self.statusBar().showMessage("Ready")
        self.statusBar().setStyleSheet("color: #888;")
    
    def _setup_timer(self):
        """Setup update timer"""
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self._update_stats)
        self.update_timer.start(500)
    
    def _apply_dark_theme(self):
        """Apply dark theme to application"""
        self.setStyleSheet("""
            QMainWindow {
                background-color: #0f0f1a;
            }
            QWidget {
                background-color: #0f0f1a;
                color: #fff;
            }
            QTabWidget::pane {
                border: 1px solid #333;
                background-color: #1a1a2e;
            }
            QTabBar::tab {
                background-color: #1a1a2e;
                color: #888;
                padding: 10px 20px;
                border: 1px solid #333;
            }
            QTabBar::tab:selected {
                background-color: #2a2a4e;
                color: #00ff88;
            }
            QGroupBox {
                border: 1px solid #333;
                margin-top: 10px;
                padding-top: 10px;
                color: #00ff88;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
            QProgressBar {
                border: 1px solid #333;
                background-color: #1a1a2e;
                text-align: center;
            }
            QProgressBar::chunk {
                background-color: #00ff88;
            }
            QMenuBar {
                background-color: #1a1a2e;
                color: #fff;
            }
            QMenuBar::item:selected {
                background-color: #2a2a4e;
            }
            QMenu {
                background-color: #1a1a2e;
                color: #fff;
                border: 1px solid #333;
            }
            QMenu::item:selected {
                background-color: #00aa55;
            }
        """)
    
    def _start_capture(self):
        """Start packet capture"""
        try:
            self.capture = PacketCapture()
            self.capture_thread = CaptureThread(self.capture)
            self.capture_thread.instance_created.connect(self._on_instance_created)
            self.capture_thread.packet_received.connect(self._on_packet)
            self.capture_thread.error_occurred.connect(self._on_error)
            self.capture_thread.start()
            
            self.start_btn.setEnabled(False)
            self.stop_btn.setEnabled(True)
            self.progress.setVisible(True)
            self.statusBar().showMessage("Capturing packets...")
            self._log("Capture started")
            
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to start capture: {e}")
            self._log(f"Error: {e}")
    
    def _stop_capture(self):
        """Stop packet capture"""
        if self.capture:
            self.capture.stop_capture()
        
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)
        self.progress.setVisible(False)
        self.statusBar().showMessage("Capture stopped")
        self._log("Capture stopped")
    
    def _export_map(self):
        """Export captured map"""
        if not self.capture or len(self.capture.data_model.instances_by_ref) == 0:
            QMessageBox.warning(self, "Warning", "No instances to export. Start capture first or load demo data.")
            return
        
        filename, _ = QFileDialog.getSaveFileName(
            self, "Export Map", "map_export.rbxlx",
            "Roblox XML (*.rbxlx);;All Files (*)"
        )
        
        if filename:
            try:
                self.capture.export_to_rbxlx(filename)
                QMessageBox.information(self, "Success", f"Map exported to {filename}")
                self._log(f"Exported to {filename}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Export failed: {e}")
                self._log(f"Export error: {e}")
    
    def _load_demo(self):
        """Load demo data"""
        self.capture = MockPacketCapture()
        self.capture.load_demo_data()
        self._update_instance_list()
        self._update_stats()
        self._log("Demo data loaded")
        self.statusBar().showMessage("Demo data loaded")
    
    def _on_instance_created(self, ref: str, class_name: str):
        """Handle new instance created"""
        item = QListWidgetItem(f"{class_name} ({ref[:16]}...)")
        self.instance_list.addItem(item)
    
    def _on_packet(self, info: str):
        """Handle packet received"""
        pass  # Don't spam log with every packet
    
    def _on_error(self, error: str):
        """Handle capture error"""
        self._log(f"Error: {error}")
    
    def _update_stats(self):
        """Update statistics display"""
        if not self.capture:
            return
        
        stats = self.capture.stats
        self.stats_labels['Packets Captured'].setText(str(stats.packets_captured))
        self.stats_labels['Packets Parsed'].setText(str(stats.packets_parsed))
        self.stats_labels['Instances Created'].setText(str(stats.instances_created))
        self.stats_labels['Properties Set'].setText(str(stats.properties_set))
        self.stats_labels['Errors'].setText(str(stats.errors))
        self.stats_labels['Duration'].setText(f"{stats.duration:.1f}s")
        
        self.instance_count_label.setText(
            f"Captured Instances: {len(self.capture.data_model.instances_by_ref)}"
        )
    
    def _update_instance_list(self):
        """Update instance list display"""
        self.instance_list.clear()
        if not self.capture:
            return
        
        for ref, inst in self.capture.data_model.instances_by_ref.items():
            name = inst.name or inst.properties.get('Name', inst.class_name)
            item = QListWidgetItem(f"{inst.class_name}: {name}")
            self.instance_list.addItem(item)
    
    def _log(self, message: str):
        """Add message to log"""
        from datetime import datetime
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.append(f"[{timestamp}] {message}")
    
    def closeEvent(self, event):
        """Handle window close"""
        if self.capture and self.capture.running:
            self.capture.stop_capture()
        event.accept()

def main():
    """Main entry point"""
    app = QApplication(sys.argv)
    app.setApplicationName("Protoon")
    app.setOrganizationName("Protoon")
    
    window = ProtoonWindow()
    window.show()
    
    sys.exit(app.exec())

if __name__ == '__main__':
    main()
