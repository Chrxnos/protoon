# -*- mode: python ; coding: utf-8 -*-
# Protoon PyInstaller Spec File

block_cipher = None

a = Analysis(
    ['protoon_gui.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('protoon', 'protoon'),
    ],
    hiddenimports=[
        'protoon',
        'protoon.raknet',
        'protoon.datamodel',
        'protoon.packet_capture',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='Protoon',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='protoon.ico' if os.path.exists('protoon.ico') else None,
)
