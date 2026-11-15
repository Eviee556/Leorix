-- One office building: floor slabs, window fins, a lobby and a roof with a parapet.

local m = mesh()
local FLOORS, FLOOR_H = 12, 0.36
local W, D = 2.4, 1.6                  -- footprint (x, z)
local BAYS_X, BAYS_Z = 8, 5            -- windows along each side

-- Step 1: The only building block we need: an axis-aligned box
local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1, (i & 2 == 0) and y0 or y1, (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end
local HX, HZ, TOP = W / 2, D / 2, FLOORS * FLOOR_H

-- Step 2: A recessed core, so the facade has depth
local INSET = 0.07
box(-HX + INSET, 0, -HZ + INSET, HX - INSET, TOP, HZ - INSET)

-- Step 3: A floor slab at every level
for k = 1, FLOORS do
  local y = k * FLOOR_H
  box(-HX, y - 0.05, -HZ, HX, y, HZ)
end

-- Step 4: Vertical fins between the windows on all four sides
local FIN = 0.035
for i = 0, BAYS_X do
  local x = -HX + i * W / BAYS_X
  box(x - FIN, FLOOR_H, HZ - INSET, x + FIN, TOP, HZ)          -- front
  box(x - FIN, FLOOR_H, -HZ, x + FIN, TOP, -HZ + INSET)        -- back
end
for i = 0, BAYS_Z do
  local z = -HZ + i * D / BAYS_Z
  box(HX - INSET, FLOOR_H, z - FIN, HX, TOP, z + FIN)          -- right
  box(-HX, FLOOR_H, z - FIN, -HX + INSET, TOP, z + FIN)        -- left
end

-- Step 5: A lobby canopy, a parapet and a rooftop plant room
box(-0.6, FLOOR_H * 0.8, HZ, 0.6, FLOOR_H * 0.8 + 0.05, HZ + 0.45)
local P = 0.05
box(-HX, TOP, -HZ, HX, TOP + 0.15, -HZ + P); box(-HX, TOP, HZ - P, HX, TOP + 0.15, HZ)
box(-HX, TOP, -HZ + P, -HX + P, TOP + 0.15, HZ - P); box(HX - P, TOP, -HZ + P, HX, TOP + 0.15, HZ - P)
box(-0.5, TOP, -0.35, 0.4, TOP + 0.4, 0.3)

m:build()
print(FLOORS, "floors")
