-- A landmark tower: floor plates that turn a little on every level, a podium and a spire.

local m = mesh()
local FLOORS, FLOOR_H = 46, 0.16
local SIZE, TWIST = 1.3, 90            -- plate width, total twist in degrees
local CORE = 0.82                      -- glass core size relative to the plates

-- Step 1: A block that can be turned around the Y axis
local function block(cx, cy, cz, sx, sy, sz, yaw)
  local c, s = math.cos(yaw), math.sin(yaw)
  local v = {}
  for i = 0, 7 do
    local x = ((i & 1 == 0) and -0.5 or 0.5) * sx
    local y = ((i & 2 == 0) and -0.5 or 0.5) * sy
    local z = ((i & 4 == 0) and -0.5 or 0.5) * sz
    v[i] = m:vert(cx + x * c - z * s, cy + y, cz + x * s + z * c)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end

-- Step 2: A two-step podium
block(0, 0.15, 0, 3.2, 0.3, 3.2, 0)
block(0, 0.42, 0, 2.4, 0.24, 2.4, 0)
local BASE = 0.54

-- Step 3: Floors: a thin slab plus a slightly smaller core, both turned by the same angle
for k = 0, FLOORS - 1 do
  local u = k / (FLOORS - 1)
  local yaw = math.rad(u * TWIST)
  local taper = 1 - 0.35 * u * u           -- narrower near the top
  local y = BASE + k * FLOOR_H
  block(0, y + FLOOR_H * 0.5, 0, SIZE * CORE * taper, FLOOR_H, SIZE * CORE * taper, yaw)
  block(0, y + FLOOR_H - 0.015, 0, SIZE * taper, 0.03, SIZE * taper, yaw)
end

-- Step 4: A four-sided spire: one square base and an apex
local top = BASE + FLOORS * FLOOR_H
local yaw, r = math.rad(TWIST) + PI / 4, SIZE * 0.65 * 0.5 * math.sqrt(2) * 0.8
local ring = {}
for i = 1, 4 do
  local a = yaw + (i - 1) * PI / 2
  ring[i] = m:vert(math.cos(a) * r, top, math.sin(a) * r)
end
local apex = m:vert(0, top + 1.6, 0)
for i = 1, 4 do
  local j = i % 4 + 1
  m:face(ring[i], apex, ring[j])
end
m:face(ring[1], ring[2], ring[3], ring[4])

m:build()
print(FLOORS, "floors")
