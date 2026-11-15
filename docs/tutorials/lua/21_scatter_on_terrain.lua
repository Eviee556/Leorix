-- Scattering trees and rocks on terrain: jittered placement that respects height and slope.

local m = mesh()
local RES, SIZE, HEIGHT, SEED = 110, 8.0, 1.6, 8
local SEA, TREE_LINE, MAX_SLOPE = 0.12, 1.05, 0.55

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

local function height(x, z)
  local h = fbm(x * 0.33, z * 0.33, 5, SEED)
  return h * h * HEIGHT * 2.2
end
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

-- Step 1: Slope from two extra height samples (a finite difference)
local function slope(x, z)
  local e = 0.05
  local dx = (height(x + e, z) - height(x - e, z)) / (2 * e)
  local dz = (height(x, z + e) - height(x, z - e)) / (2 * e)
  return math.sqrt(dx * dx + dz * dz)
end

-- Step 2: A tree (trunk plus two cones) and a low rock (a squashed, jittered octahedron)
local function cone(x, y0, z, r, hgt, sides)
  local ring = {}
  for i = 1, sides do
    local a = (i - 1) / sides * 2 * PI
    ring[i] = m:vert(x + math.cos(a) * r, y0, z + math.sin(a) * r)
  end
  local tip = m:vert(x, y0 + hgt, z)
  for i = 1, sides do m:face(ring[i], tip, ring[i % sides + 1]) end
  m:face(table.unpack(ring))
end
local function tree(x, y, z, s)
  cone(x, y - 0.05, z, 0.03 * s, 0.3 * s, 5)      -- trunk
  cone(x, y + 0.12 * s, z, 0.16 * s, 0.38 * s, 7)
  cone(x, y + 0.3 * s, z, 0.11 * s, 0.3 * s, 7)
end
local function rock(x, y, z, s, k)
  local j = function(n) return 1 + (hash(k, n, SEED + 3) - 0.5) * 0.6 end
  local v = {
    m:vert(x + s * j(1), y, z), m:vert(x - s * j(2), y, z),
    m:vert(x, y + s * 0.7 * j(3), z), m:vert(x, y - s * 0.3, z),
    m:vert(x, y, z + s * j(5)), m:vert(x, y, z - s * j(6))}
  local F = {{1, 3, 5}, {5, 3, 2}, {2, 3, 6}, {6, 3, 1}, {5, 4, 1}, {2, 4, 5}, {6, 4, 2}, {1, 4, 6}}
  for _, f in ipairs(F) do m:face(v[f[1]], v[f[2]], v[f[3]]) end
end

-- Step 3: A jittered grid: one candidate per cell, moved randomly inside its cell
local CELL = 0.32
local n = math.floor(SIZE / CELL)
local trees, rocks = 0, 0
for gi = 0, n - 1 do
  for gj = 0, n - 1 do
    local x = (gi + 0.15 + 0.7 * hash(gi, gj, SEED + 1)) * CELL - SIZE / 2
    local z = (gj + 0.15 + 0.7 * hash(gi, gj, SEED + 2)) * CELL - SIZE / 2
    local y, s = height(x, z), slope(x, z)

    -- Step 4: The rules: trees on gentle ground below the tree line, rocks above it
    local roll = hash(gi, gj, SEED + 4)
    if y > SEA and s < MAX_SLOPE and y < TREE_LINE and roll < 0.55 then
      tree(x, y, z, 0.8 + 0.6 * hash(gi, gj, SEED + 5)); trees = trees + 1
    elseif y >= TREE_LINE and roll < 0.35 then
      rock(x, y, z, 0.08 + 0.06 * hash(gi, gj, SEED + 6), gi * 1000 + gj); rocks = rocks + 1
    end
  end
end

m:build()
print(trees, "trees,", rocks, "rocks")
