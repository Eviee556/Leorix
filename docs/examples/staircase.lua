-- Spiral staircase: a central column plus steps rotated around it.
-- Shows a reusable box() helper that emits 8 verts and 6 outward-facing quads.

local STEPS  = 24        -- number of treads
local TURN   = 1.5       -- full turns from bottom to top
local RISE   = 0.18      -- height of each step
local INNER  = 0.25      -- column radius
local OUTER  = 1.6       -- outer edge of the treads
local DEPTH  = 0.06      -- tread thickness

local m = mesh()

-- Axis-aligned box from (x0,y0,z0) to (x1,y1,z1), then rotated about Y by `angle`.
local function box(x0, y0, z0, x1, y1, z1, angle)
  local c, s = math.cos(angle or 0), math.sin(angle or 0)
  local v = {}
  for i = 0, 7 do
    local x = (i & 1 == 0) and x0 or x1
    local y = (i & 2 == 0) and y0 or y1
    local z = (i & 4 == 0) and z0 or z1
    v[i] = m:vert(x * c - z * s, y, x * s + z * c)
  end
  -- counter-clockwise when seen from outside, so normals point out
  m:face(v[0], v[4], v[6], v[2])  -- -X
  m:face(v[1], v[3], v[7], v[5])  -- +X
  m:face(v[0], v[1], v[5], v[4])  -- -Y
  m:face(v[2], v[6], v[7], v[3])  -- +Y
  m:face(v[0], v[2], v[3], v[1])  -- -Z
  m:face(v[4], v[5], v[7], v[6])  -- +Z
end

-- the central column as an n-sided prism with n-gon caps
local SIDES, H = 16, STEPS * RISE + 0.4
local bottom, top = {}, {}
for i = 0, SIDES - 1 do
  local a = i / SIDES * 2 * PI
  bottom[i + 1] = m:vert(math.cos(a) * INNER, 0, math.sin(a) * INNER)
  top[i + 1]    = m:vert(math.cos(a) * INNER, H, math.sin(a) * INNER)
end
for i = 1, SIDES do
  local j = i % SIDES + 1
  m:face(bottom[i], top[i], top[j], bottom[j])
end
local cap = {}
for i = SIDES, 1, -1 do cap[#cap + 1] = top[i] end
m:face(table.unpack(cap))       -- top cap, one n-gon
m:face(table.unpack(bottom))    -- bottom cap

-- the treads
for i = 0, STEPS - 1 do
  local y = (i + 1) * RISE
  local angle = i / STEPS * TURN * 2 * PI
  box(INNER * 0.5, y - DEPTH, -0.22, OUTER, y, 0.22, angle)
end

m:build()
print(STEPS, "steps")
