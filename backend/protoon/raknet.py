"""
RakNet Protocol Parser for Roblox UDP Packets
Based on roblox-dissector by Gskartwii
"""
import struct
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Any, Tuple
from enum import IntEnum
import zlib

class PacketType(IntEnum):
    ID_CONNECTED_PING = 0x00
    ID_UNCONNECTED_PING = 0x01
    ID_CONNECTED_PONG = 0x03
    ID_DETECT_LOST_CONNECTIONS = 0x04
    ID_OPEN_CONNECTION_REQUEST_1 = 0x05
    ID_OPEN_CONNECTION_REPLY_1 = 0x06
    ID_OPEN_CONNECTION_REQUEST_2 = 0x07
    ID_OPEN_CONNECTION_REPLY_2 = 0x08
    ID_CONNECTION_REQUEST = 0x09
    ID_CONNECTION_REQUEST_ACCEPTED = 0x10
    ID_CONNECTION_ATTEMPT_FAILED = 0x11
    ID_NEW_INCOMING_CONNECTION = 0x13
    ID_DISCONNECTION_NOTIFICATION = 0x15
    ID_INVALID_PASSWORD = 0x18
    ID_TIMESTAMP = 0x1B
    ID_UNCONNECTED_PONG = 0x1C
    ID_SET_GLOBALS = 0x81
    ID_TEACH_DESCRIPTOR_DICTIONARIES = 0x82
    ID_DATA = 0x83
    ID_MARKER = 0x84
    ID_PHYSICS = 0x85
    ID_TOUCHES = 0x86
    ID_CHAT_ALL = 0x87
    ID_CHAT_TEAM = 0x88
    ID_REPORT_ABUSE = 0x89
    ID_SUBMIT_TICKET = 0x8A
    ID_CHAT_GAME = 0x8B
    ID_CHAT_PLAYER = 0x8C
    ID_CLUSTER = 0x8D
    ID_PROTOCOL_MISMATCH = 0x8E
    ID_PREFERRED_SPAWN_NAME = 0x8F
    ID_PROTOCOL_SYNC = 0x90
    ID_SCHEMA_SYNC = 0x91
    ID_PLACEID_VERIFICATION = 0x92
    ID_DICTIONARY_FORMAT = 0x93
    ID_HASH_MISMATCH = 0x94
    ID_SECURITYKEY_MISMATCH = 0x95
    ID_REQUEST_STATS = 0x96
    ID_NEW_SCHEMA = 0x97

class ReplicationSubpacket(IntEnum):
    ID_REPLIC_DELETE_INSTANCE = 0x01
    ID_REPLIC_NEW_INSTANCE = 0x02
    ID_REPLIC_PROP = 0x03
    ID_REPLIC_MARKER = 0x04
    ID_REPLIC_PING = 0x05
    ID_REPLIC_PING_BACK = 0x06
    ID_REPLIC_EVENT = 0x07
    ID_REPLIC_REQUEST_CHARACTER = 0x08
    ID_REPLIC_ROCKY = 0x09
    ID_REPLIC_CFRAME_ACK = 0x0A
    ID_REPLIC_GETI = 0x0B
    ID_REPLIC_TAG = 0x0C
    ID_REPLIC_STREAM_DATA = 0x0D
    ID_REPLIC_ATOMIC = 0x0E
    ID_REPLIC_HASH = 0x0F
    ID_REPLIC_INSTANCE_REMOVAL = 0x10
    ID_REPLIC_ADD_CHILD = 0x11
    ID_REPLIC_REMOVE_CHILD = 0x12
    ID_REPLIC_JOIN_DATA = 0x13
    ID_REPLIC_END_JOIN = 0x14

class PropertyType(IntEnum):
    NIL = 0
    STRING = 1
    STRING_NO_CACHE = 2
    PROTECTED_STRING_0 = 3
    PROTECTED_STRING_1 = 4
    PROTECTED_STRING_2 = 5
    PROTECTED_STRING_3 = 6
    ENUM = 7
    BINARY_STRING = 8
    BOOL = 9
    INT = 10
    FLOAT = 11
    DOUBLE = 12
    UDIM = 13
    UDIM2 = 14
    RAY = 15
    FACES = 16
    AXES = 17
    BRICK_COLOR = 18
    COLOR3 = 19
    COLOR3_UINT8 = 20
    VECTOR2 = 21
    SIMPLE_VECTOR3 = 22
    COMPLICATED_VECTOR3 = 23
    VECTOR2_INT16 = 24
    VECTOR3_INT16 = 25
    SIMPLE_CFRAME = 26
    COMPLICATED_CFRAME = 27
    INSTANCE = 28
    TUPLE = 29
    ARRAY = 30
    DICTIONARY = 31
    MAP = 32
    CONTENT = 33
    SYSTEM_ADDRESS = 34
    NUMBER_SEQUENCE = 35
    NUMBER_SEQUENCE_KEYPOINT = 36
    NUMBER_RANGE = 37
    COLOR_SEQUENCE = 38
    COLOR_SEQUENCE_KEYPOINT = 39
    RECT2D = 40
    PHYSICAL_PROPERTIES = 41
    REGION3 = 42
    REGION3_INT16 = 43
    INT64 = 44
    PATH_WAYPOINT = 45
    SHARED_STRING = 46
    LUAU_STRING = 47
    DATETIME = 48
    OPTIMIZED_STRING = 49

OFFLINE_MESSAGE_ID = bytes([0x00, 0xFF, 0xFF, 0x00, 0xFE, 0xFE, 0xFE, 0xFE, 
                            0xFD, 0xFD, 0xFD, 0xFD, 0x12, 0x34, 0x56, 0x78])

@dataclass
class RakNetFlags:
    is_valid: bool = False
    is_ack: bool = False
    is_nak: bool = False
    is_pair: bool = False
    continuous_send: bool = False
    needs_b_and_as: bool = False

@dataclass
class ACKRange:
    min_val: int
    max_val: int

@dataclass
class RakNetLayer:
    flags: RakNetFlags
    acks: List[ACKRange] = field(default_factory=list)
    datagram_number: int = 0
    payload: bytes = b''

@dataclass
class ReliablePacket:
    reliability_type: int = 0
    has_split_packet: bool = False
    message_number: int = 0
    sequencing_index: int = 0
    ordering_index: int = 0
    ordering_channel: int = 0
    split_packet_count: int = 0
    split_packet_id: int = 0
    split_packet_index: int = 0
    data: bytes = b''

class BitstreamReader:
    """Binary stream reader for RakNet packets"""
    
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.bit_pos = 0
    
    def remaining(self) -> int:
        return len(self.data) - self.pos
    
    def read_bytes(self, count: int) -> bytes:
        if self.pos + count > len(self.data):
            raise ValueError(f"Not enough data: need {count}, have {self.remaining()}")
        result = self.data[self.pos:self.pos + count]
        self.pos += count
        return result
    
    def read_byte(self) -> int:
        return self.read_bytes(1)[0]
    
    def read_bool_byte(self) -> bool:
        return self.read_byte() != 0
    
    def read_uint16_be(self) -> int:
        return struct.unpack('>H', self.read_bytes(2))[0]
    
    def read_uint16_le(self) -> int:
        return struct.unpack('<H', self.read_bytes(2))[0]
    
    def read_uint24_le(self) -> int:
        data = self.read_bytes(3)
        return data[0] | (data[1] << 8) | (data[2] << 16)
    
    def read_uint32_be(self) -> int:
        return struct.unpack('>I', self.read_bytes(4))[0]
    
    def read_uint32_le(self) -> int:
        return struct.unpack('<I', self.read_bytes(4))[0]
    
    def read_uint64_be(self) -> int:
        return struct.unpack('>Q', self.read_bytes(8))[0]
    
    def read_int32_be(self) -> int:
        return struct.unpack('>i', self.read_bytes(4))[0]
    
    def read_float32_be(self) -> float:
        return struct.unpack('>f', self.read_bytes(4))[0]
    
    def read_float64_be(self) -> float:
        return struct.unpack('>d', self.read_bytes(8))[0]
    
    def read_varint(self) -> int:
        """Read a variable-length unsigned integer"""
        result = 0
        shift = 0
        while True:
            byte = self.read_byte()
            result |= (byte & 0x7F) << shift
            if (byte & 0x80) == 0:
                break
            shift += 7
        return result
    
    def read_varsint(self) -> int:
        """Read a variable-length signed integer (zigzag encoded)"""
        val = self.read_varint()
        return (val >> 1) ^ -(val & 1)
    
    def read_var_length_string(self) -> str:
        length = self.read_varint()
        return self.read_bytes(length).decode('utf-8', errors='replace')
    
    def read_ascii(self, length: int) -> str:
        return self.read_bytes(length).decode('ascii', errors='replace')
    
    def read_raknet_flags(self) -> RakNetFlags:
        byte = self.read_byte()
        flags = RakNetFlags()
        flags.is_valid = (byte & 0x80) != 0
        flags.is_ack = (byte & 0x40) != 0
        flags.is_nak = (byte & 0x20) != 0
        flags.is_pair = (byte & 0x10) != 0
        flags.continuous_send = (byte & 0x08) != 0
        flags.needs_b_and_as = (byte & 0x04) != 0
        return flags
    
    def read_vector3(self) -> Tuple[float, float, float]:
        x = self.read_float32_be()
        y = self.read_float32_be()
        z = self.read_float32_be()
        return (x, y, z)
    
    def read_vector2(self) -> Tuple[float, float]:
        x = self.read_float32_be()
        y = self.read_float32_be()
        return (x, y)
    
    def read_color3(self) -> Tuple[float, float, float]:
        r = self.read_float32_be()
        g = self.read_float32_be()
        b = self.read_float32_be()
        return (r, g, b)
    
    def read_cframe(self) -> Dict[str, Any]:
        position = self.read_vector3()
        special = self.read_byte()
        
        if special > 0:
            if special > 36:
                raise ValueError(f"Invalid special rotation matrix: {special}")
            rotation = self._lookup_rot_matrix(special - 1)
        else:
            rotation = [self.read_float32_be() for _ in range(9)]
        
        return {'position': position, 'rotation': rotation}
    
    def _lookup_rot_matrix(self, special: int) -> List[float]:
        """Lookup special rotation matrix by index"""
        special_columns = [
            [1, 0, 0], [0, 1, 0], [0, 0, 1],
            [-1, 0, 0], [0, -1, 0], [0, 0, -1]
        ]
        col0 = special_columns[special // 6]
        col1 = special_columns[special % 6]
        
        ret = [
            col0[0], col1[0], 0,
            col0[1], col1[1], 0,
            col0[2], col1[2], 0
        ]
        ret[2] = col0[1] * col1[2] - col1[1] * col0[2]
        ret[5] = col1[0] * col0[2] - col0[0] * col1[2]
        ret[8] = col0[0] * col1[1] - col1[0] * col0[1]
        return ret

def is_offline_message(data: bytes) -> bool:
    """Check if packet is an offline message"""
    if len(data) < 1 + len(OFFLINE_MESSAGE_ID):
        return False
    return data[1:1+len(OFFLINE_MESSAGE_ID)] == OFFLINE_MESSAGE_ID

def decode_raknet_layer(data: bytes) -> Optional[RakNetLayer]:
    """Decode a RakNet packet layer"""
    try:
        reader = BitstreamReader(data)
        flags = reader.read_raknet_flags()
        
        if not flags.is_valid:
            return None
        
        layer = RakNetLayer(flags=flags)
        
        if flags.is_ack or flags.is_nak:
            ack_count = reader.read_uint16_be()
            for _ in range(ack_count):
                min_eq_max = reader.read_bool_byte()
                min_val = reader.read_uint24_le()
                if min_eq_max:
                    max_val = min_val
                else:
                    max_val = reader.read_uint24_le()
                layer.acks.append(ACKRange(min_val, max_val))
        else:
            layer.datagram_number = reader.read_uint24_le()
            layer.payload = reader.read_bytes(reader.remaining())
        
        return layer
    except Exception as e:
        return None

def decode_reliable_packet(data: bytes) -> Optional[ReliablePacket]:
    """Decode a reliable packet from RakNet payload"""
    try:
        reader = BitstreamReader(data)
        packet = ReliablePacket()
        
        # Read reliability flags
        flags = reader.read_byte()
        packet.reliability_type = (flags >> 5) & 0x7
        packet.has_split_packet = (flags & 0x10) != 0
        
        # Data bit length
        data_bit_length = reader.read_uint16_be()
        data_byte_length = (data_bit_length + 7) // 8
        
        # Reliable packets have message number
        if packet.reliability_type in [2, 3, 4, 6, 7]:
            packet.message_number = reader.read_uint24_le()
        
        # Sequenced packets have sequencing index
        if packet.reliability_type in [1, 4]:
            packet.sequencing_index = reader.read_uint24_le()
        
        # Ordered packets have ordering info
        if packet.reliability_type in [1, 3, 4, 7]:
            packet.ordering_index = reader.read_uint24_le()
            packet.ordering_channel = reader.read_byte()
        
        # Split packet info
        if packet.has_split_packet:
            packet.split_packet_count = reader.read_uint32_be()
            packet.split_packet_id = reader.read_uint16_be()
            packet.split_packet_index = reader.read_uint32_be()
        
        packet.data = reader.read_bytes(data_byte_length)
        return packet
    except Exception as e:
        return None

def get_packet_name(packet_type: int) -> str:
    """Get human-readable name for packet type"""
    try:
        return PacketType(packet_type).name
    except ValueError:
        return f"UNKNOWN_0x{packet_type:02X}"
