"""
UDP Packet Capture for Roblox
Intercepts game traffic to extract instances and map data
"""
import socket
import threading
import struct
import time
from typing import Callable, Optional, Dict, List, Any
from dataclasses import dataclass, field
from collections import defaultdict
import logging

from .raknet import (
    decode_raknet_layer, decode_reliable_packet, 
    PacketType, ReplicationSubpacket, BitstreamReader,
    get_packet_name
)
from .datamodel import DataModel, Instance

logger = logging.getLogger(__name__)

@dataclass
class CaptureStats:
    """Statistics for packet capture session"""
    packets_captured: int = 0
    packets_parsed: int = 0
    instances_created: int = 0
    properties_set: int = 0
    errors: int = 0
    start_time: float = field(default_factory=time.time)
    
    @property
    def duration(self) -> float:
        return time.time() - self.start_time
    
    @property
    def packets_per_second(self) -> float:
        if self.duration > 0:
            return self.packets_captured / self.duration
        return 0

@dataclass
class NetworkSchema:
    """Network schema received from server"""
    instances: List[Dict[str, Any]] = field(default_factory=list)
    properties: List[Dict[str, Any]] = field(default_factory=list)
    events: List[Dict[str, Any]] = field(default_factory=list)
    enums: List[Dict[str, Any]] = field(default_factory=list)
    content_prefixes: List[str] = field(default_factory=list)
    optimized_strings: List[str] = field(default_factory=list)

class SplitPacketBuffer:
    """Buffer for reassembling split packets"""
    
    def __init__(self):
        self.buffers: Dict[int, Dict[int, bytes]] = defaultdict(dict)
        self.counts: Dict[int, int] = {}
    
    def add_split(self, split_id: int, index: int, count: int, data: bytes) -> Optional[bytes]:
        """Add a split packet piece, return complete data if all pieces received"""
        self.buffers[split_id][index] = data
        self.counts[split_id] = count
        
        if len(self.buffers[split_id]) == count:
            # All pieces received, reassemble
            result = b''
            for i in range(count):
                result += self.buffers[split_id][i]
            
            # Clean up
            del self.buffers[split_id]
            del self.counts[split_id]
            return result
        
        return None

class PacketCapture:
    """Main packet capture class"""
    
    def __init__(self):
        self.data_model = DataModel()
        self.schema = NetworkSchema()
        self.stats = CaptureStats()
        self.split_buffer = SplitPacketBuffer()
        self.running = False
        self.socket: Optional[socket.socket] = None
        self._capture_thread: Optional[threading.Thread] = None
        self.on_instance_created: Optional[Callable[[Instance], None]] = None
        self.on_packet_received: Optional[Callable[[bytes, str], None]] = None
        
        # String cache for deferred strings
        self.string_cache: Dict[str, str] = {}
        
        # Roblox server ports typically in this range
        self.roblox_ports = set(range(49152, 65536))
    
    def start_capture(self, interface: str = "0.0.0.0", port: int = 0):
        """Start capturing UDP packets"""
        self.running = True
        self.stats = CaptureStats()
        
        try:
            # Create raw socket for UDP capture
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind((interface, port))
            self.socket.settimeout(1.0)
            
            self._capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
            self._capture_thread.start()
            
            logger.info(f"Started capture on {interface}:{port}")
        except Exception as e:
            logger.error(f"Failed to start capture: {e}")
            self.running = False
            raise
    
    def stop_capture(self):
        """Stop capturing packets"""
        self.running = False
        if self.socket:
            self.socket.close()
        if self._capture_thread:
            self._capture_thread.join(timeout=2.0)
        logger.info(f"Capture stopped. Stats: {self.stats}")
    
    def _capture_loop(self):
        """Main capture loop"""
        while self.running:
            try:
                data, addr = self.socket.recvfrom(65535)
                self.stats.packets_captured += 1
                
                # Check if this might be Roblox traffic
                if addr[1] in self.roblox_ports or self._is_roblox_packet(data):
                    self._process_packet(data, addr)
                    
            except socket.timeout:
                continue
            except Exception as e:
                self.stats.errors += 1
                logger.debug(f"Capture error: {e}")
    
    def _is_roblox_packet(self, data: bytes) -> bool:
        """Check if packet looks like Roblox traffic"""
        if len(data) < 1:
            return False
        
        # Check for valid RakNet flag
        first_byte = data[0]
        return (first_byte & 0x80) != 0 or first_byte in [0x05, 0x06, 0x07, 0x08]
    
    def _process_packet(self, data: bytes, addr: tuple):
        """Process a captured packet"""
        try:
            layer = decode_raknet_layer(data)
            if not layer:
                return
            
            if layer.flags.is_ack or layer.flags.is_nak:
                return  # Skip ACKs/NAKs
            
            if not layer.payload:
                return
            
            # Process reliability layer
            self._process_payload(layer.payload)
            
        except Exception as e:
            self.stats.errors += 1
            logger.debug(f"Error processing packet: {e}")
    
    def _process_payload(self, payload: bytes):
        """Process RakNet payload"""
        pos = 0
        while pos < len(payload):
            try:
                packet = decode_reliable_packet(payload[pos:])
                if not packet:
                    break
                
                # Handle split packets
                if packet.has_split_packet:
                    complete_data = self.split_buffer.add_split(
                        packet.split_packet_id,
                        packet.split_packet_index,
                        packet.split_packet_count,
                        packet.data
                    )
                    if complete_data:
                        self._process_game_packet(complete_data)
                else:
                    self._process_game_packet(packet.data)
                
                # Move to next packet (simplified - actual implementation needs proper length tracking)
                pos += len(packet.data) + 10  # Approximate header size
                
            except Exception as e:
                self.stats.errors += 1
                break
    
    def _process_game_packet(self, data: bytes):
        """Process a complete game packet"""
        if len(data) < 1:
            return
        
        packet_type = data[0]
        self.stats.packets_parsed += 1
        
        if self.on_packet_received:
            self.on_packet_received(data, get_packet_name(packet_type))
        
        try:
            if packet_type == PacketType.ID_DATA:
                self._process_data_packet(data[1:])
            elif packet_type == PacketType.ID_SCHEMA_SYNC:
                self._process_schema_sync(data[1:])
            elif packet_type == PacketType.ID_PROTOCOL_SYNC:
                self._process_protocol_sync(data[1:])
        except Exception as e:
            self.stats.errors += 1
            logger.debug(f"Error processing game packet 0x{packet_type:02X}: {e}")
    
    def _process_data_packet(self, data: bytes):
        """Process ID_DATA packet (0x83) - contains replication subpackets"""
        reader = BitstreamReader(data)
        
        while reader.remaining() > 0:
            try:
                subpacket_type = reader.read_byte()
                
                if subpacket_type == ReplicationSubpacket.ID_REPLIC_NEW_INSTANCE:
                    self._process_new_instance(reader)
                elif subpacket_type == ReplicationSubpacket.ID_REPLIC_PROP:
                    self._process_property_update(reader)
                elif subpacket_type == ReplicationSubpacket.ID_REPLIC_DELETE_INSTANCE:
                    self._process_delete_instance(reader)
                elif subpacket_type == ReplicationSubpacket.ID_REPLIC_EVENT:
                    self._process_event(reader)
                elif subpacket_type == ReplicationSubpacket.ID_REPLIC_JOIN_DATA:
                    self._process_join_data(reader)
                else:
                    # Skip unknown subpacket types
                    break
                    
            except Exception as e:
                logger.debug(f"Error processing subpacket: {e}")
                break
    
    def _process_new_instance(self, reader: BitstreamReader):
        """Process ID_REPLIC_NEW_INSTANCE subpacket"""
        try:
            # Read instance reference
            ref = self._read_reference(reader)
            
            # Read schema index
            schema_idx = reader.read_uint16_be()
            
            # Get class name from schema
            class_name = "Unknown"
            if schema_idx < len(self.schema.instances):
                class_name = self.schema.instances[schema_idx].get('name', 'Unknown')
            
            # Create instance
            instance = self.data_model.get_or_create_instance(ref, class_name)
            instance.class_name = class_name
            
            # Read delete on disconnect flag
            delete_on_disconnect = reader.read_bool_byte()
            
            # Read properties (simplified)
            self._read_instance_properties(reader, instance, schema_idx)
            
            # Read parent reference
            parent_ref = self._read_reference(reader)
            if parent_ref and parent_ref != "null":
                parent = self.data_model.get_instance(parent_ref)
                if parent:
                    parent.add_child(instance)
                else:
                    # Parent might be a service
                    parent = self.data_model.get_or_create_instance(parent_ref, "DataModel")
                    parent.add_child(instance)
                    if parent not in self.data_model.services:
                        self.data_model.add_service(parent)
            
            self.stats.instances_created += 1
            
            if self.on_instance_created:
                self.on_instance_created(instance)
                
        except Exception as e:
            logger.debug(f"Error creating instance: {e}")
    
    def _process_property_update(self, reader: BitstreamReader):
        """Process ID_REPLIC_PROP subpacket"""
        try:
            ref = self._read_reference(reader)
            prop_idx = reader.read_uint16_be()
            
            instance = self.data_model.get_instance(ref)
            if instance:
                # Read property value based on schema
                # This is simplified - full implementation needs property type handling
                self.stats.properties_set += 1
        except Exception as e:
            logger.debug(f"Error updating property: {e}")
    
    def _process_delete_instance(self, reader: BitstreamReader):
        """Process ID_REPLIC_DELETE_INSTANCE subpacket"""
        try:
            ref = self._read_reference(reader)
            # Mark instance as deleted (don't actually remove for full capture)
        except Exception as e:
            logger.debug(f"Error deleting instance: {e}")
    
    def _process_event(self, reader: BitstreamReader):
        """Process ID_REPLIC_EVENT subpacket"""
        # Events are typically not needed for map saving
        pass
    
    def _process_join_data(self, reader: BitstreamReader):
        """Process ID_REPLIC_JOIN_DATA subpacket - bulk instance data"""
        try:
            # Join data contains compressed instance data
            # This is sent when first joining a game
            compressed_size = reader.read_uint32_be()
            # Rest is zstd compressed data
        except Exception as e:
            logger.debug(f"Error processing join data: {e}")
    
    def _process_schema_sync(self, data: bytes):
        """Process ID_SCHEMA_SYNC packet"""
        try:
            reader = BitstreamReader(data)
            
            # Read class count
            class_count = reader.read_uint32_be()
            
            for _ in range(class_count):
                class_name = reader.read_var_length_string()
                unknown = reader.read_uint16_be()
                
                # Read property count
                prop_count = reader.read_uint16_be()
                properties = []
                
                for _ in range(prop_count):
                    prop_name = reader.read_var_length_string()
                    prop_type = reader.read_byte()
                    properties.append({
                        'name': prop_name,
                        'type': prop_type
                    })
                
                # Read event count
                event_count = reader.read_uint16_be()
                events = []
                
                for _ in range(event_count):
                    event_name = reader.read_var_length_string()
                    events.append({'name': event_name})
                
                self.schema.instances.append({
                    'name': class_name,
                    'properties': properties,
                    'events': events
                })
            
            logger.info(f"Received schema with {class_count} classes")
            
        except Exception as e:
            logger.debug(f"Error processing schema sync: {e}")
    
    def _process_protocol_sync(self, data: bytes):
        """Process ID_PROTOCOL_SYNC packet"""
        # Contains protocol version and flags
        pass
    
    def _read_reference(self, reader: BitstreamReader) -> str:
        """Read an instance reference"""
        # References are variable-length encoded
        ref_val = reader.read_varint()
        if ref_val == 0:
            return "null"
        return f"RBX{ref_val:08X}"
    
    def _read_instance_properties(self, reader: BitstreamReader, instance: Instance, schema_idx: int):
        """Read properties for an instance based on schema"""
        if schema_idx >= len(self.schema.instances):
            return
        
        schema = self.schema.instances[schema_idx]
        properties = schema.get('properties', [])
        
        for prop_info in properties:
            try:
                prop_name = prop_info['name']
                prop_type = prop_info['type']
                
                value = self._read_property_value(reader, prop_type)
                if value is not None:
                    instance.properties[prop_name] = value
                    
            except Exception as e:
                break
    
    def _read_property_value(self, reader: BitstreamReader, prop_type: int) -> Any:
        """Read a property value based on type"""
        from .raknet import PropertyType
        
        try:
            if prop_type == PropertyType.BOOL:
                return reader.read_bool_byte()
            elif prop_type == PropertyType.INT:
                return reader.read_varsint()
            elif prop_type == PropertyType.FLOAT:
                return reader.read_float32_be()
            elif prop_type == PropertyType.DOUBLE:
                return reader.read_float64_be()
            elif prop_type == PropertyType.STRING:
                return reader.read_var_length_string()
            elif prop_type == PropertyType.SIMPLE_VECTOR3:
                return reader.read_vector3()
            elif prop_type == PropertyType.VECTOR2:
                return reader.read_vector2()
            elif prop_type == PropertyType.COLOR3:
                return reader.read_color3()
            elif prop_type == PropertyType.SIMPLE_CFRAME:
                return reader.read_cframe()
            elif prop_type == PropertyType.BRICK_COLOR:
                return reader.read_uint16_be()
            else:
                return None
        except:
            return None
    
    def process_pcap_file(self, filepath: str):
        """Process a PCAP file for offline analysis"""
        try:
            import dpkt
            
            with open(filepath, 'rb') as f:
                pcap = dpkt.pcap.Reader(f)
                
                for timestamp, buf in pcap:
                    try:
                        eth = dpkt.ethernet.Ethernet(buf)
                        if isinstance(eth.data, dpkt.ip.IP):
                            ip = eth.data
                            if isinstance(ip.data, dpkt.udp.UDP):
                                udp = ip.data
                                self._process_packet(bytes(udp.data), (str(ip.src), udp.sport))
                    except:
                        continue
                        
        except ImportError:
            logger.warning("dpkt not installed - PCAP processing unavailable")
        except Exception as e:
            logger.error(f"Error processing PCAP file: {e}")
    
    def export_to_rbxlx(self, filepath: str):
        """Export captured DataModel to RBXLX file"""
        xml_content = self.data_model.to_rbxlx()
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(xml_content)
        logger.info(f"Exported {len(self.data_model.instances_by_ref)} instances to {filepath}")

class MockPacketCapture(PacketCapture):
    """Mock packet capture for testing and demo purposes"""
    
    def __init__(self):
        super().__init__()
        self._demo_data_loaded = False
    
    def load_demo_data(self):
        """Load demo data to show functionality"""
        if self._demo_data_loaded:
            return
        
        # Create demo services
        workspace = Instance(class_name="Workspace", name="Workspace")
        workspace.is_service = True
        
        # Add some parts
        for i in range(5):
            part = Instance(
                class_name="Part",
                name=f"Part{i}",
                properties={
                    "Position": (i * 10, 5, 0),
                    "Size": (4, 4, 4),
                    "BrickColor": 194,
                    "Anchored": True
                }
            )
            workspace.add_child(part)
        
        # Add a model with children
        model = Instance(class_name="Model", name="DemoModel")
        for i in range(3):
            child_part = Instance(
                class_name="Part",
                name=f"ModelPart{i}",
                properties={
                    "Position": (i * 5, 10, 10),
                    "Size": (2, 2, 2),
                    "BrickColor": 21
                }
            )
            model.add_child(child_part)
        workspace.add_child(model)
        
        self.data_model.add_service(workspace)
        
        # Add lighting
        lighting = Instance(class_name="Lighting", name="Lighting", properties={
            "Ambient": (0.5, 0.5, 0.5),
            "Brightness": 1.0,
            "TimeOfDay": "14:00:00"
        })
        lighting.is_service = True
        self.data_model.add_service(lighting)
        
        self._demo_data_loaded = True
        self.stats.instances_created = len(self.data_model.instances_by_ref)
