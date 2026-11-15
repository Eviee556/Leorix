-- A river valley: carve a winding channel into the terrain with a distance function.

local m = mesh()
local RES, SIZE, HEIGHT, SEED = 140, 8.0, 2.0, 33
local RIVER_W, VALLEY_W, BED = 0.25, 1.6, 0.12

-- Toolkit from the fBm terrain tutorial: hash, value noise and fBm
-- A hash: whole-number coordinates in, repeatable value in [0, 1) out
local function hash(ix, iz, seed)
  local h = (ix * 374761393 + iz * 668265263 + seed * 2246822519) & 0xffffffff
  h = ((h ~ (h >> 13)) * 1274126177) & 0xffffffff
  return ((h ~ (h >> 16)) & 0xffffff) / 16777216
end

-- Value noise: blend the hashes at the four surrounding grid corners
local function smooth(t) return t * t * (3 - 2 * t) end
local function lerp(a, b, t) return a + (b - a) * t end
local function noise(x, z, seed)
  local ix, iz = math.floor(x), math.floor(z)
  local fx, fz = smooth(x - ix), smooth(z - iz)
  return lerp(lerp(hash(ix, iz, seed), hash(ix + 1, iz, seed), fx),
              lerp(hash(ix, iz + 1, seed), hash(ix + 1, iz + 1, seed), fx), fz)
end

-- fBm: add octaves, each twice as detailed and half as strong
local function fbm(x, z, octaves, seed)
  local sum, amp, freq, total = 0, 0.5, 1, 0
  for o = 1, octaves do
    sum = sum + amp * noise(x * freq, z * freq, seed + o)
    total = total + amp
    amp, freq = amp * 0.5, freq * 2
  end
  return sum / total                      -- back to roughly 0..1
end

-- Step 1: Describe the river as a curve: x as a function of z
local function river_x(z)
  return math.sin(z * 0.55) * 1.3 + math.sin(z * 1.4 + 1.0) * 0.35
end

-- Step 2: How far is a point from the river? (horizontal distance is good enough)
local function river_dist(x, z)
  return math.abs(x - river_x(z))
end

-- Step 3: Hills everywhere, then pull them down near the river
local function height(x, z)
  local hills = fbm(x * 0.35, z * 0.35, 6, SEED) * HEIGHT * 1.4
  local d = river_dist(x, z)
  local valley = smooth(math.min(1, math.max(0, (d - RIVER_W) / VALLEY_W)))   -- 0 in the river, 1 far away
  local banks = BED + 0.25 * valley
  return banks + (hills - 0.2) * valley * valley
end

-- Step 4: Build the block; the water plane sits just above the river bed
local function heightfield(fn, res, size, base)
  local idx, border, bottom = {}, {}, {}
  for j = 0, res do
    idx[j] = {}
    for i = 0, res do
      local x, z = (i / res - 0.5) * size, (j / res - 0.5) * size
      idx[j][i] = m:vert(x, fn(x, z), z)
    end
  end
  for j = 0, res - 1 do
    for i = 0, res - 1 do
      m:face(idx[j][i], idx[j + 1][i], idx[j + 1][i + 1], idx[j][i + 1])
    end
  end
  -- walk around the edge: +x along the front, +z up the right, back along the far side, down the left
  for i = 0, res do border[#border + 1] = {i, 0} end
  for j = 1, res do border[#border + 1] = {res, j} end
  for i = res - 1, 0, -1 do border[#border + 1] = {i, res} end
  for j = res - 1, 1, -1 do border[#border + 1] = {0, j} end
  local top = {}
  for k, c in ipairs(border) do
    local x, z = (c[1] / res - 0.5) * size, (c[2] / res - 0.5) * size
    top[k] = idx[c[2]][c[1]]
    bottom[k] = m:vert(x, base, z)
  end
  for k = 1, #border do
    local n = k % #border + 1
    m:face(top[k], top[n], bottom[n], bottom[k])
  end
  m:face(table.unpack(bottom))
end
heightfield(height, RES, SIZE, -0.3)

m:build()
print("water level", BED + 0.05)
