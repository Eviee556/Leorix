-- An island: noise that fades to the seabed towards the edges, with a warped coastline.

local m = mesh()
local RES, SIZE, HEIGHT, SEED = 140, 9.0, 1.8, 21
local SEA = 0.0                        -- water level; render a water plane at this height

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

-- Step 1: A radial falloff: 1 in the middle, 0 near the edge
local function falloff(x, z)
  local d = math.sqrt(x * x + z * z) / (SIZE * 0.5)
  return 1 - smooth(math.min(1, math.max(0, (d - 0.25) / 0.65)))
end

-- Step 2: Domain warping: nudge the coordinates with noise so the coast isn't a circle
local function warp(x, z)
  local wx = (fbm(x * 0.4 + 31, z * 0.4, 3, SEED + 7) - 0.5) * 2.2
  local wz = (fbm(x * 0.4, z * 0.4 + 17, 3, SEED + 9) - 0.5) * 2.2
  return x + wx, z + wz
end

-- Step 3: Land = noise times falloff, shifted down so the outside sinks under the sea
local function height(x, z)
  local wx, wz = warp(x, z)
  local land = fbm(wx * 0.45, wz * 0.45, 6, SEED) * falloff(wx, wz)
  local h = (land * 1.9 - 0.32) * HEIGHT
  if h > 0 then h = h * (0.6 + 0.4 * land) end   -- gentle beaches, steeper peaks
  return h
end

-- Step 4: The seabed stays a solid block, just like the land
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
heightfield(height, RES, SIZE, -0.9)

m:build()
print("sea level", SEA)
