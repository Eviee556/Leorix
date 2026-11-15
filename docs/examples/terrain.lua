-- Terrain: a height field from fractal value noise.
-- Lua has no built-in noise, so this builds one from a hash, smooth interpolation and octaves.

local RES     = 96        -- quads per side
local SIZE    = 6.0       -- world size
local HEIGHT  = 1.1
local SEED    = 7

-- deterministic pseudo-random value in [0, 1) for integer lattice point (x, z)
local function hash(x, z)
  local h = (x * 374761393 + z * 668265263 + SEED * 2147483647) & 0xffffffff
  h = ((h ~ (h >> 13)) * 1274126177) & 0xffffffff
  return (h ~ (h >> 16)) / 4294967296
end

local function smooth(t) return t * t * (3 - 2 * t) end

local function noise(x, z)
  local xi, zi = math.floor(x), math.floor(z)
  local xf, zf = smooth(x - xi), smooth(z - zi)
  local a, b = hash(xi, zi),     hash(xi + 1, zi)
  local c, d = hash(xi, zi + 1), hash(xi + 1, zi + 1)
  local top = a + (b - a) * xf
  local bot = c + (d - c) * xf
  return top + (bot - top) * zf
end

local function fbm(x, z)                 -- fractal Brownian motion: 4 octaves
  local sum, amp, freq = 0, 0.5, 1
  for _ = 1, 4 do
    sum = sum + amp * noise(x * freq, z * freq)
    amp, freq = amp * 0.5, freq * 2
  end
  return sum
end

local m = mesh()
local idx = {}
for j = 0, RES do
  idx[j] = {}
  for i = 0, RES do
    local u, v = i / RES, j / RES
    local x, z = (u - 0.5) * SIZE, (v - 0.5) * SIZE
    local h = fbm(u * 3.5, v * 3.5)
    h = h * h * HEIGHT * 2.2                 -- squaring makes valleys flatter, peaks sharper
    local edge = math.min(u, v, 1 - u, 1 - v) -- fade to 0 at the border
    h = h * math.min(1, edge * 8)
    idx[j][i] = m:vert(x, h, z)
  end
end

for j = 0, RES - 1 do
  for i = 0, RES - 1 do
    -- winding chosen so the faces point up (+Y)
    m:face(idx[j][i], idx[j + 1][i], idx[j + 1][i + 1], idx[j][i + 1])
  end
end

m:build()
