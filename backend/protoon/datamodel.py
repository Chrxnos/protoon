"""
Roblox DataModel - Instance hierarchy representation
For reconstructing game worlds from network packets
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Any
import uuid
import xml.etree.ElementTree as ET
from xml.dom import minidom

@dataclass
class Instance:
    """Represents a Roblox Instance"""
    class_name: str
    name: str = ""
    properties: Dict[str, Any] = field(default_factory=dict)
    children: List['Instance'] = field(default_factory=list)
    parent: Optional['Instance'] = None
    ref: str = field(default_factory=lambda: f"RBX{uuid.uuid4().hex.upper()}")
    is_service: bool = False
    
    def add_child(self, child: 'Instance'):
        """Add a child instance"""
        if child.parent:
            child.parent.children.remove(child)
        child.parent = self
        self.children.append(child)
    
    def find_first_child(self, name: str) -> Optional['Instance']:
        """Find first child with given name"""
        for child in self.children:
            if child.name == name or child.properties.get('Name') == name:
                return child
        return None
    
    def get_full_name(self) -> str:
        """Get full path name"""
        parts = []
        inst = self
        while inst:
            parts.insert(0, inst.name or inst.class_name)
            inst = inst.parent
        return '.'.join(parts)
    
    def get_descendants(self) -> List['Instance']:
        """Get all descendants"""
        result = []
        for child in self.children:
            result.append(child)
            result.extend(child.get_descendants())
        return result

@dataclass
class DataModel:
    """Represents a Roblox DataModel (game/place)"""
    services: List[Instance] = field(default_factory=list)
    instances_by_ref: Dict[str, Instance] = field(default_factory=dict)
    
    def add_service(self, service: Instance):
        """Add a service to the DataModel"""
        service.is_service = True
        self.services.append(service)
        self.instances_by_ref[service.ref] = service
    
    def get_or_create_instance(self, ref: str, class_name: str = "") -> Instance:
        """Get existing instance or create new one"""
        if ref in self.instances_by_ref:
            return self.instances_by_ref[ref]
        
        instance = Instance(class_name=class_name, ref=ref)
        self.instances_by_ref[ref] = instance
        return instance
    
    def get_instance(self, ref: str) -> Optional[Instance]:
        """Get instance by reference"""
        return self.instances_by_ref.get(ref)
    
    def to_rbxlx(self) -> str:
        """Export to RBXLX (XML) format"""
        root = ET.Element('roblox')
        root.set('xmlns:xmime', 'http://www.w3.org/2005/05/xmlmime')
        root.set('xmlns:xsi', 'http://www.w3.org/2001/XMLSchema-instance')
        root.set('xsi:noNamespaceSchemaLocation', 'http://www.roblox.com/roblox.xsd')
        root.set('version', '4')
        
        # Add external section
        external = ET.SubElement(root, 'External')
        external.text = 'null'
        external = ET.SubElement(root, 'External')
        external.text = 'nil'
        
        # Add all services and their children
        for service in self.services:
            self._instance_to_xml(root, service)
        
        # Pretty print
        xml_str = ET.tostring(root, encoding='unicode')
        try:
            dom = minidom.parseString(xml_str)
            return dom.toprettyxml(indent='  ')
        except:
            return xml_str
    
    def _instance_to_xml(self, parent_elem: ET.Element, instance: Instance):
        """Convert instance to XML element"""
        item = ET.SubElement(parent_elem, 'Item')
        item.set('class', instance.class_name)
        item.set('referent', instance.ref)
        
        props = ET.SubElement(item, 'Properties')
        
        # Always add Name property
        name_prop = ET.SubElement(props, 'string')
        name_prop.set('name', 'Name')
        name_prop.text = instance.name or instance.class_name
        
        # Add other properties
        for prop_name, prop_value in instance.properties.items():
            if prop_name == 'Name':
                continue
            self._property_to_xml(props, prop_name, prop_value)
        
        # Add children
        for child in instance.children:
            self._instance_to_xml(item, child)
    
    def _property_to_xml(self, parent_elem: ET.Element, name: str, value: Any):
        """Convert property to XML element"""
        if value is None:
            return
        
        if isinstance(value, bool):
            elem = ET.SubElement(parent_elem, 'bool')
            elem.set('name', name)
            elem.text = 'true' if value else 'false'
        
        elif isinstance(value, int):
            elem = ET.SubElement(parent_elem, 'int')
            elem.set('name', name)
            elem.text = str(value)
        
        elif isinstance(value, float):
            elem = ET.SubElement(parent_elem, 'float')
            elem.set('name', name)
            elem.text = str(value)
        
        elif isinstance(value, str):
            elem = ET.SubElement(parent_elem, 'string')
            elem.set('name', name)
            elem.text = value
        
        elif isinstance(value, dict):
            # Handle complex types
            if 'position' in value and 'rotation' in value:
                # CFrame
                elem = ET.SubElement(parent_elem, 'CoordinateFrame')
                elem.set('name', name)
                pos = value['position']
                rot = value['rotation']
                ET.SubElement(elem, 'X').text = str(pos[0])
                ET.SubElement(elem, 'Y').text = str(pos[1])
                ET.SubElement(elem, 'Z').text = str(pos[2])
                for i, r in enumerate(rot):
                    ET.SubElement(elem, f'R{i//3}{i%3}').text = str(r)
            
            elif 'X' in value and 'Y' in value and 'Z' in value:
                # Vector3
                elem = ET.SubElement(parent_elem, 'Vector3')
                elem.set('name', name)
                ET.SubElement(elem, 'X').text = str(value['X'])
                ET.SubElement(elem, 'Y').text = str(value['Y'])
                ET.SubElement(elem, 'Z').text = str(value['Z'])
            
            elif 'X' in value and 'Y' in value:
                # Vector2
                elem = ET.SubElement(parent_elem, 'Vector2')
                elem.set('name', name)
                ET.SubElement(elem, 'X').text = str(value['X'])
                ET.SubElement(elem, 'Y').text = str(value['Y'])
            
            elif 'R' in value and 'G' in value and 'B' in value:
                # Color3
                elem = ET.SubElement(parent_elem, 'Color3')
                elem.set('name', name)
                ET.SubElement(elem, 'R').text = str(value['R'])
                ET.SubElement(elem, 'G').text = str(value['G'])
                ET.SubElement(elem, 'B').text = str(value['B'])
        
        elif isinstance(value, tuple):
            if len(value) == 3:
                # Vector3 or Color3
                elem = ET.SubElement(parent_elem, 'Vector3')
                elem.set('name', name)
                ET.SubElement(elem, 'X').text = str(value[0])
                ET.SubElement(elem, 'Y').text = str(value[1])
                ET.SubElement(elem, 'Z').text = str(value[2])
            elif len(value) == 2:
                # Vector2
                elem = ET.SubElement(parent_elem, 'Vector2')
                elem.set('name', name)
                ET.SubElement(elem, 'X').text = str(value[0])
                ET.SubElement(elem, 'Y').text = str(value[1])

class InstanceDictionary:
    """Manages instance references for serialization"""
    
    def __init__(self):
        self.counter = 0
    
    def new_reference(self) -> str:
        """Generate a new unique reference"""
        ref = f"RBX{self.counter:032X}"
        self.counter += 1
        return ref
