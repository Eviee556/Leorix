-- Parametric vase: a surface of revolution.
-- A profile function r(t) gives the radius at height t; we sweep it around Y.

local SEGMENTS = 48      -- around
local RINGS    = 40      -- up
local HEIGHT   = 3.0

local function radius(t)            -- t goes 0 (base) .. 1 (lip)
  return 0.55
       + 0.45 * math.sin(t * PI * 1.15)       -- belly
       - 0.25 * math.sin(t * PI * 2.6) * t    -- neck
       + 0.03 * math.sin(t * PI * 18)         -- fine ridges
end

local m = mesh()
local ring = {}
for j = 0, RINGS do
  local t = j / RINGS
  local r = radius(t)
  ring[j] = {}
  for i = 0, SEGMENTS - 1 do
    local a = i / SEGMENTS * 2 * PI
    ring[j][i] = m:vert(math.cos(a) * r, t * HEIGHT, math.sin(a) * r)
  end
end

-- quads between neighbouring rings
for j = 0, RINGS - 1 do
  for i = 0, SEGMENTS - 1 do
    local k = (i + 1) % SEGMENTS
    m:face(ring[j][i], ring[j + 1][i], ring[j + 1][k], ring[j][k])
  end
end

-- closed base as a single n-gon (the lip stays open)
local base = {}
for i = 0, SEGMENTS - 1 do base[#base + 1] = ring[0][i] end
m:face(table.unpack(base))

m:build()
