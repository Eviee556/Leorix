-- A skyline from a deterministic hash: the same seed always gives the same city.
-- Heights rise towards downtown; taller towers get setbacks (stepped tiers).

local m = mesh()
local N, LOT, GAP, SEED = 14, 0.55, 0.12, 2024

local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1, (i & 2 == 0) and y0 or y1, (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end

-- Step 1: A hash turns whole numbers into a repeatable "random" value in [0, 1)
local function hash(x, z, k)
  local h = (x * 374761393 + z * 668265263 + (k or 0) * 1442695041 + SEED * 2246822519) & 0xffffffff
  h = ((h ~ (h >> 13)) * 1274126177) & 0xffffffff
  return ((h ~ (h >> 16)) & 0xffffff) / 16777216
end

-- Step 2: Downtown is in the middle: height falls off with distance
local function downtown(x, z)
  local d = math.sqrt(x * x + z * z)
  return math.exp(-d * d * 0.035)
end

-- Step 3: Each lot gets a height, a footprint and a number of tiers from the hash
local PITCH = LOT + GAP
local half = (N - 1) * PITCH / 2
box(-half - 0.6, -0.04, -half - 0.6, half + 0.6, 0, half + 0.6)       -- ground plate
for i = 0, N - 1 do
  for j = 0, N - 1 do
    local cx, cz = i * PITCH - half, j * PITCH - half
    local r = hash(i, j, 0)
    local h = 0.25 + (0.4 + 4.2 * downtown(cx, cz)) * (0.3 + 0.7 * r)
    local w = LOT * (0.75 + 0.25 * hash(i, j, 1)) / 2
    local tiers = (h > 2.0) and 3 or ((h > 1.0) and 2 or 1)

    -- Step 4: Stack the tiers, each narrower and shorter than the one below
    local y = 0
    for t = 1, tiers do
      local th = h * ({0.55, 0.3, 0.15})[t] / ({0.55, 0.85, 1.0})[tiers]
      box(cx - w, y, cz - w, cx + w, y + th, cz + w)
      y = y + th
      w = w * 0.72
    end

    -- Step 5: A few of the tallest get an antenna (a second hash decides which)
    if h > 3.2 and hash(i, j, 2) > 0.6 then box(cx - 0.02, y, cz - 0.02, cx + 0.02, y + 0.9, cz + 0.02) end
  end
end

m:build()
