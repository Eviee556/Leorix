## Leorix Engine Specification (v1.0)

# 1. File Format .leorix
The .leorix format is a ZIP-compressed container optimized for non-destructive editing and high-speed asset retrieval.

Structure
manifest.json: The central authority. Contains:

Layer hierarchy (ID, Name, Opacity, Blending Mode, Visibility).

Canvas metadata (Width, Height, Color Space, Bit Depth).

Project settings.

layers/: Directory of assets. Each layer is split by map type:

L{ID}_Albedo.webp (or .png)

L{ID}_Normal.bin (Raw binary for precision)

L{ID}_Roughness.webp

L{ID}_Metallic.webp

L{ID}_AO.webp

L{ID}_Displacement.bin

brushes/: JSON-encoded brush definitions (spacing, jitter, falloff).

cache/: (Optional) Mipmaps and thumbnails for rapid UI loading.

# 2. Multi-Map Layer Stack
Each layer is a container for a full PBR texture set.

Blending Logic
Perceptual Maps (Albedo, Roughness, Metallic, AO): Standard Alpha Compositing.

Vector Maps (Normal, Displacement): Reoriented Normal Mapping (RNM).

Normal maps must be blended as vectors to prevent the "flattening" effect common in standard alpha blending.

# # 3. Command Pattern (Undo/Redo)
To maintain Affected Area memory complexity:

Dirty Tiles: Only areas modified by a command are processed.

Command Object: Stores:

Metadata (Command type, Timestamp, Layer ID).

LZ4 Compressed Delta: The bitwise difference of the changed tiles.

Application: Undo/Redo is performed by applying/reversing the compressed delta to the specific tiles in the SharedArrayBuffer.

# 4. Process Architecture
Main Process (Electron): File I/O, Window management, OS Dialogs.

Renderer Process (UI): React/HTML/CSS for the interface.

Worker/GPU Process: High-speed pixel manipulation and WebGL/WebGPU rendering.

Data Bridge: Large pixel buffers reside in SharedArrayBuffer to allow zero-copy access between the UI and the Computational Engine.
