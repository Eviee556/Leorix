-- fBm value-noise terrain, built as a solid diorama block.

local m = mesh()
local RES, SIZE, HEIGHT, SEED = 120, 8.0, 2.2, 5

-- Step 1: A hash: whole-number coordinates in, repeatable value in [0, 1) out
local function hash(ix, iz, seed)
  local h = (ix * 374761393 + iz * 668265263 + seed * 2246822519) & 0xffffffff
  h = ((h ~ (h >> 13)) * 1274126177) & 0xffffffff
  return ((h ~ (h >> 16)) & 0xffffff) / 16777216
end

-- Step 2: Value noise: blend the hashes at the four surrounding grid corners
local function smooth(t) return t * t * (3 - 2 * t) end
local function lerp(a, b, t) return a + (b - a) * t end
local function noise(x, z, seed)
  local ix, iz = math.floor(x), math.floor(z)
  local fx, fz = smooth(x - ix), smooth(z - iz)
  return lerp(lerp(hash(ix, iz, seed), hash(ix + 1, iz, seed), fx),
              lerp(hash(ix, iz + 1, seed), hash(ix + 1, iz + 1, seed), fx), fz)
end

-- Step 3: fBm: add octaves, each twice as detailed and half as strong
local function fbm(x, z, octaves, seed)
  local sum, amp, freq, total = 0, 0.5, 1, 0
  for o = 1, octaves do
    sum = sum + amp * noise(x * freq, z * freq, seed + o)
    total = total + amp
    amp, freq = amp * 0.5, freq * 2
  end
  return sum / total                      -- back to roughly 0..1
end

-- Step 4: The height function: this is the part you will change most
local function height(x, z)
  local h = fbm(x * 0.35, z * 0.35, 6, SEED)
  return (h * h) * HEIGHT * 1.6           -- squaring flattens valleys and sharpens peaks
end

-- Step 5: Sample a grid, and close it into a solid block with a skirt and a base
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
