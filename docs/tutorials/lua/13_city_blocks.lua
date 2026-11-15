-- A grid of city blocks: roads, raised sidewalks, lane markings and simple buildings.

local m = mesh()
local NB = 4                           -- blocks per side
local BLOCK, ROAD = 2.0, 0.6           -- block size and road width
local CURB = 0.06                      -- sidewalk height

local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1, (i & 2 == 0) and y0 or y1, (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end

-- Step 1: Work out where every block starts
local PITCH = BLOCK + ROAD
local SPAN = NB * PITCH + ROAD
local ORIGIN = -SPAN / 2
local function block_corner(i, j)       -- lower-left corner of block (i, j), 0-based
  return ORIGIN + ROAD + i * PITCH, ORIGIN + ROAD + j * PITCH
end

-- Step 2: One flat slab of road under everything
box(ORIGIN, -0.05, ORIGIN, ORIGIN + SPAN, 0, ORIGIN + SPAN)

-- Step 3: Dashed lane markings down the middle of every road
for r = 0, NB do
  local c = ORIGIN + ROAD / 2 + r * PITCH          -- road centre line
  local t = ORIGIN
  while t < ORIGIN + SPAN - 0.3 do
    box(c - 0.02, 0, t, c + 0.02, 0.005, t + 0.18)  -- along z
    box(t, 0, c - 0.02, t + 0.18, 0.005, c + 0.02)  -- along x
    t = t + 0.35
  end
end

-- Step 4: Raised sidewalks, then 2 x 2 lots per block
local heights = {0.8, 1.6, 1.1, 2.4}
for i = 0, NB - 1 do
  for j = 0, NB - 1 do
    local x0, z0 = block_corner(i, j)
    box(x0, 0, z0, x0 + BLOCK, CURB, z0 + BLOCK)
    local lot = (BLOCK - 0.3) / 2
    for a = 0, 1 do
      for b = 0, 1 do
        local lx, lz = x0 + 0.15 + a * lot, z0 + 0.15 + b * lot
        local h = heights[(i + 2 * j + a + 3 * b) % #heights + 1]   -- a fixed pattern for now
        box(lx + 0.06, CURB, lz + 0.06, lx + lot - 0.06, CURB + h, lz + lot - 0.06)
      end
    end
  end
end

m:build()
print(NB * NB, "blocks")
