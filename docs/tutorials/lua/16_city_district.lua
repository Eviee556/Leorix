-- A full district: roads and blocks, hashed towers with setbacks, a park with trees,
-- and a twisted landmark tower in the central square.

local m = mesh()
local NB, BLOCK, ROAD, CURB, SEED = 5, 2.2, 0.6, 0.06, 77

-- Step 1: The toolkit from the previous tutorials
local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1, (i & 2 == 0) and y0 or y1, (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end
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
local function hash(x, z, k)
  local h = (x * 374761393 + z * 668265263 + (k or 0) * 1442695041 + SEED * 2246822519) & 0xffffffff
  h = ((h ~ (h >> 13)) * 1274126177) & 0xffffffff
  return ((h ~ (h >> 16)) & 0xffffff) / 16777216
end

-- Step 2: Each kind of block is its own function
local function tower_lot(x0, z0, size, i, j, a, b)
  local cx, cz = x0 + size / 2, z0 + size / 2
  local dist = math.sqrt(cx * cx + cz * cz)
  local h = (0.4 + 3.0 * math.exp(-dist * dist * 0.02)) * (0.35 + 0.65 * hash(i * 2 + a, j * 2 + b, 0))
  local w, y = size * 0.42, CURB
  local tiers = h > 1.6 and 3 or (h > 0.8 and 2 or 1)
  for t = 1, tiers do
    local th = h / tiers * (t == 1 and 1.4 or 0.8)
    box(cx - w, y, cz - w, cx + w, y + th, cz + w)
    y, w = y + th, w * 0.75
  end
end

local function tree(x, z, s)
  local function cone(y0, r, hgt, sides)
    local ring = {}
    for i = 1, sides do
      local a = (i - 1) / sides * 2 * PI
      ring[i] = m:vert(x + math.cos(a) * r, y0, z + math.sin(a) * r)
    end
    local tip = m:vert(x, y0 + hgt, z)
    for i = 1, sides do m:face(ring[i], tip, ring[i % sides + 1]) end
    m:face(table.unpack(ring))
  end
  box(x - 0.03 * s, CURB, z - 0.03 * s, x + 0.03 * s, CURB + 0.25 * s, z + 0.03 * s)
  cone(CURB + 0.18 * s, 0.22 * s, 0.4 * s, 7)
  cone(CURB + 0.38 * s, 0.16 * s, 0.35 * s, 7)
end

local function park(x0, z0, i, j)
  for k = 0, 17 do
    local tx = x0 + 0.25 + hash(i, j, 10 + k) * (BLOCK - 0.5)
    local tz = z0 + 0.25 + hash(i, j, 40 + k) * (BLOCK - 0.5)
    tree(tx, tz, 0.8 + 0.5 * hash(i, j, 70 + k))
  end
end

local function landmark(cx, cz)
  block(cx, CURB + 0.1, cz, 1.6, 0.2, 1.6, 0)
  local floors = 40
  for k = 0, floors - 1 do
    local u = k / (floors - 1)
    local yaw, sz = math.rad(u * 100), 0.9 * (1 - 0.3 * u * u)
    local y = CURB + 0.2 + k * 0.14
    block(cx, y + 0.07, cz, sz * 0.82, 0.14, sz * 0.82, yaw)
    block(cx, y + 0.125, cz, sz, 0.025, sz, yaw)
  end
  box(cx - 0.02, CURB + 0.2 + floors * 0.14, cz - 0.02, cx + 0.02, CURB + 0.2 + floors * 0.14 + 1.2, cz + 0.02)
end

-- Step 3: Roads, sidewalks and lane markings
local PITCH = BLOCK + ROAD
local SPAN = NB * PITCH + ROAD
local O = -SPAN / 2
box(O, -0.05, O, O + SPAN, 0, O + SPAN)
for r = 0, NB do
  local c = O + ROAD / 2 + r * PITCH
  for t = O + 0.1, O + SPAN - 0.3, 0.4 do
    box(c - 0.02, 0, t, c + 0.02, 0.005, t + 0.2)
    box(t, 0, c - 0.02, t + 0.2, 0.005, c + 0.02)
  end
end

-- Step 4: Decide what goes on every block, then build it
local mid = (NB - 1) // 2
for i = 0, NB - 1 do
  for j = 0, NB - 1 do
    local x0, z0 = O + ROAD + i * PITCH, O + ROAD + j * PITCH
    box(x0, 0, z0, x0 + BLOCK, CURB, z0 + BLOCK)
    if i == mid and j == mid then
      landmark(x0 + BLOCK / 2, z0 + BLOCK / 2)
    elseif hash(i, j, 99) < 0.18 then
      park(x0, z0, i, j)
    else
      local lot = BLOCK / 2
      for a = 0, 1 do
        for b = 0, 1 do tower_lot(x0 + a * lot, z0 + b * lot, lot, i, j, a, b) end
      end
    end
  end
end

m:build()
