-- A ringed planet: your first Leorix script.
-- Built-in primitives appear at the origin, so a sphere and a torus
-- share the same centre: exactly what a planet and its ring need.

-- Step 1: Ask Leorix for a shape
local verts, tris = sphere(0.75, 48, 24)      -- radius, segments around, rings top to bottom
print("planet:", verts, "verts,", tris, "triangles")

-- Step 2: Add a second shape around the first
local rv, rt = torus(1.35, 0.06, 96, 8)        -- ring radius, tube radius, segments
print("ring:", rv, "verts,", rt, "triangles")

-- Step 3: Add a thinner, wider ring for depth
torus(1.75, 0.03, 128, 6)
