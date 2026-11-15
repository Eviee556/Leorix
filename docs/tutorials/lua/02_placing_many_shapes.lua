-- A ring of standing stones with lintels: placing many shapes.
-- Primitives can't be moved, so we write our own block() that can.

local m = mesh()

-- Step 1: A block you can place, size and turn
-- (cx, cy, cz) is the centre, (sx, sy, sz) the size, yaw the turn around Y in radians.
local function block(cx, cy, cz, sx, sy, sz, yaw)
  local c, s = math.cos(yaw or 0), math.sin(yaw or 0)
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

-- Step 2: Walk around a circle
local COUNT, RADIUS = 12, 3.0
local tops = {}
for i = 0, COUNT - 1 do
  local a = i / COUNT * 2 * PI
  local x, z = math.cos(a) * RADIUS, math.sin(a) * RADIUS
  -- Step 3: Give every stone its own height
  local h = 1.6 + 0.35 * math.sin(i * 2.3)
  block(x, h / 2, z, 0.45, h, 0.7, a)          -- turn each stone to face the centre
  tops[i] = {x = x, z = z, h = h}
end

-- Step 4: Lay a lintel across every second pair
for i = 0, COUNT - 1, 2 do
  local p, q = tops[i], tops[(i + 1) % COUNT]
  local dx, dz = q.x - p.x, q.z - p.z
  local len = math.sqrt(dx * dx + dz * dz) + 0.5
  local y = math.max(p.h, q.h) + 0.15
  block((p.x + q.x) / 2, y, (p.z + q.z) / 2, len, 0.3, 0.55, math.atan(dz, dx))
end

-- Step 5: A low altar stone in the middle
block(0, 0.2, 0, 1.4, 0.4, 0.7, 0.3)

m:build()
print(COUNT, "stones")
