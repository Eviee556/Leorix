-- Lantern Isle: a floating island at dusk, built entirely from this script.
-- Render it headless:  leorix --headless-render docs/showcase/lantern_isle.lua hero.png --width 1920 --height 1080 --spp 2048 --denoise 1.5
-- Every mesh takes the material() that was set before it was made.

-- ---------------------------------------------------------------- helpers
local sin, cos, sqrt, floor, abs, min, max = math.sin, math.cos, math.sqrt, math.floor, math.abs, math.min, math.max

local seed = 7
local function rand()            -- small deterministic LCG so the island is the same every run
  seed = (seed * 1103515245 + 12345) % 2147483648
  return seed / 2147483648
end
local function rr(a, b) return a + (b - a) * rand() end

local function hash(x, y, z)
  local h = sin(x * 127.1 + y * 311.7 + z * 74.7) * 43758.5453
  return h - floor(h)
end
local function smooth(t) return t * t * (3 - 2 * t) end
local function vnoise(x, y, z)   -- value noise in 0..1
  local xi, yi, zi = floor(x), floor(y), floor(z)
  local xf, yf, zf = smooth(x - xi), smooth(y - yi), smooth(z - zi)
  local function L(a, b, t) return a + (b - a) * t end
  local function c(i, j, k) return hash(xi + i, yi + j, zi + k) end
  return L(L(L(c(0,0,0), c(1,0,0), xf), L(c(0,1,0), c(1,1,0), xf), yf),
           L(L(c(0,0,1), c(1,0,1), xf), L(c(0,1,1), c(1,1,1), xf), yf), zf)
end
local function fbm(x, y, z, oct)
  local s, a, f = 0, 0.5, 1
  for _ = 1, oct do s = s + a * vnoise(x * f, y * f, z * f); a = a * 0.5; f = f * 2.03 end
  return s
end

-- a box centred at (cx, cy, cz) with half sizes (hx, hy, hz), turned ry radians about Y
local function box(cx, cy, cz, hx, hy, hz, ry)
  ry = ry or 0
  local cr, sr = cos(ry), sin(ry)
  local m = mesh()
  local v = {}
  for i = 0, 7 do
    local x = (i % 2 == 0) and -hx or hx
    local y = (floor(i / 2) % 2 == 0) and -hy or hy
    local z = (i < 4) and -hz or hz
    v[i] = m:vert(cx + x * cr + z * sr, cy + y, cz - x * sr + z * cr)
  end
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:build()
end

-- a sphere-ish blob: f(dir) returns the radius scale for each direction
local function blob(cx, cy, cz, sx, sy, sz, seg, f)
  local m = mesh()
  local rings = floor(seg / 2)
  local idx = {}
  for r = 0, rings do
    local th = PI * r / rings
    idx[r] = {}
    for s = 0, seg - 1 do
      local ph = 2 * PI * s / seg
      local dx, dy, dz = sin(th) * cos(ph), cos(th), sin(th) * sin(ph)
      local k = f and f(dx, dy, dz) or 1
      idx[r][s] = m:vert(cx + dx * sx * k, cy + dy * sy * k, cz + dz * sz * k)
    end
  end
  for r = 0, rings - 1 do
    for s = 0, seg - 1 do
      local s2 = (s + 1) % seg
      local a, b, c, d = idx[r][s], idx[r][s2], idx[r + 1][s2], idx[r + 1][s]
      if r == 0 then m:face(a, c, d)
      elseif r == rings - 1 then m:face(a, b, c)
      else m:face(a, b, c, d) end
    end
  end
  m:build()
end
local function ball(cx, cy, cz, r, seg) blob(cx, cy, cz, r, r, r, seg or 24) end

-- a closed cylinder or frustum along Y from y0 to y1
local function frustum(cx, y0, cz, r0, y1, r1, seg)
  local m = mesh()
  local bot, top = {}, {}
  for s = 0, seg - 1 do
    local a = 2 * PI * s / seg
    bot[s] = m:vert(cx + cos(a) * r0, y0, cz + sin(a) * r0)
    top[s] = m:vert(cx + cos(a) * r1, y1, cz + sin(a) * r1)
  end
  for s = 0, seg - 1 do
    local s2 = (s + 1) % seg
    m:face(bot[s], top[s], top[s2], bot[s2])
  end
  local cb, ct = {}, {}
  for s = 0, seg - 1 do cb[#cb + 1] = bot[seg - 1 - s]; ct[#ct + 1] = top[s] end
  if r0 > 0 then m:face(table.unpack(cb)) end
  if r1 > 0 then m:face(table.unpack(ct)) end
  m:build()
end

-- ---------------------------------------------------------------- the island
local R = 4.2
local function groundY(x, z)        -- gentle rolling top, a little hill behind the cottage
  local d = sqrt(x * x + z * z) / R
  local hill = 0.55 * math.exp(-((x + 0.9) ^ 2 + (z + 1.4) ^ 2) / 2.2)
  return 0.25 * (fbm(x * 0.6, 0, z * 0.6, 3) - 0.5) + hill - 0.18 * d * d
end
local function edgeR(a) return R * (0.86 + 0.22 * fbm(cos(a) * 1.3 + 5, sin(a) * 1.3, 2, 3)) end

-- grassy top: a disc grid in polar rings
material{color={0.28, 0.46, 0.17}, roughness=0.85, smooth=true}
do
  local m = mesh()
  local rings, seg = 26, 96
  local centre = m:vert(0, groundY(0, 0), 0)
  local ring = {}
  for r = 1, rings do
    ring[r] = {}
    for s = 0, seg - 1 do
      local a = 2 * PI * s / seg
      local rad = edgeR(a) * r / rings
      local x, z = cos(a) * rad, sin(a) * rad
      ring[r][s] = m:vert(x, groundY(x, z), z)
    end
  end
  for s = 0, seg - 1 do m:face(centre, ring[1][(s + 1) % seg], ring[1][s]) end
  for r = 1, rings - 1 do
    for s = 0, seg - 1 do
      local s2 = (s + 1) % seg
      m:face(ring[r][s], ring[r][s2], ring[r + 1][s2], ring[r + 1][s])
    end
  end
  m:build()
end

-- rocky underside: rings that shrink to a jagged point, with layered strata
material{color={0.42, 0.33, 0.26}, roughness=0.9, smooth=true}
do
  local m = mesh()
  local layers, seg = 28, 96
  local ring = {}
  for l = 0, layers do
    local t = l / layers
    ring[l] = {}
    for s = 0, seg - 1 do
      local a = 2 * PI * s / seg
      local x0, z0 = cos(a), sin(a)
      local base = edgeR(a)
      local taper = (1 - t) ^ 1.35
      local strata = 0.08 * sin(t * 40 + fbm(x0 * 2, t * 4, z0 * 2, 2) * 6)
      local jag = 0.55 * (fbm(x0 * 2.2 + 9, t * 3.5, z0 * 2.2, 4) - 0.5)
      local rad = base * (taper * (1 + jag) + strata * taper)
      if l == 0 then rad = base end
      local y = groundY(x0 * base, z0 * base) - 0.02 - t * 5.2 - 0.6 * fbm(x0 * 3, 1, z0 * 3, 2) * t
      ring[l][s] = m:vert(x0 * max(rad, 0.02), y, z0 * max(rad, 0.02))
    end
  end
  for l = 0, layers - 1 do
    for s = 0, seg - 1 do
      local s2 = (s + 1) % seg
      m:face(ring[l][s], ring[l + 1][s], ring[l + 1][s2], ring[l][s2])
    end
  end
  m:build()
end

-- ---------------------------------------------------------------- the cottage (focal point)
local hx, hz = -0.6, -0.9
local gy = groundY(hx, hz)
local rot = 0.35
-- stone base and plaster walls
material{color={0.55, 0.52, 0.48}, roughness=0.9}
box(hx, gy + 0.12, hz, 1.05, 0.14, 0.8, rot)
material{color={0.86, 0.80, 0.68}, roughness=0.8}
box(hx, gy + 0.85, hz, 0.95, 0.62, 0.7, rot)
-- timber beams
material{color={0.25, 0.16, 0.10}, roughness=0.7}
local cr, sr = cos(rot), sin(rot)
local function local2world(lx, lz) return hx + lx * cr + lz * sr, hz - lx * sr + lz * cr end
for _, lx in ipairs({-0.95, -0.32, 0.32, 0.95}) do
  local x, z = local2world(lx, 0.71)
  box(x, gy + 0.85, z, 0.05, 0.62, 0.03, rot)
end
do local x, z = local2world(0, 0.72); box(x, gy + 1.45, z, 1.0, 0.05, 0.03, rot) end
-- steep roof: two slabs
material{color={0.55, 0.20, 0.13}, roughness=0.65}
do
  local m = mesh()
  local function V(lx, y, lz) local x, z = local2world(lx, lz); return m:vert(x, gy + y, z) end
  local e, w, f, b, ridge = 1.15, -1.15, 0.92, -0.92, 2.35
  local a, bb, c, d = V(w, 1.45, f), V(e, 1.45, f), V(e, ridge, 0), V(w, ridge, 0)
  local a2, b2 = V(w, 1.45, b), V(e, 1.45, b)
  m:face(a, bb, c, d); m:face(b2, a2, d, c)
  -- thickness so it reads as a slab from below
  local a3, b3, c3, d3 = V(w, 1.37, f), V(e, 1.37, f), V(e, ridge - 0.1, 0), V(w, ridge - 0.1, 0)
  local a4, b4 = V(w, 1.37, b), V(e, 1.37, b)
  m:face(c3, b3, a3, d3); m:face(d3, a4, b4, c3)
  m:face(a3, b3, bb, a); m:face(b4, a4, a2, b2)
  m:build()
end
-- gable walls
material{color={0.86, 0.80, 0.68}, roughness=0.8}
do
  local m = mesh()
  for _, lx in ipairs({-0.95, 0.95}) do
    local function V(lz, y) local x, z = local2world(lx, lz); return m:vert(x, gy + y, z) end
    m:face(V(-0.7, 1.45), V(0.7, 1.45), V(0, 2.2))
  end
  m:build()
end
-- chimney
material{color={0.45, 0.40, 0.38}, roughness=0.9}
do local x, z = local2world(0.55, -0.35); box(x, gy + 2.15, z, 0.14, 0.45, 0.14, rot) end
-- warm glowing windows and door (the light source that sells the dusk)
material{color={1, 0.8, 0.5}, emission={1.0, 0.60, 0.25}, strength=26}
for _, lx in ipairs({-0.62, 0.62}) do
  local x, z = local2world(lx, 0.705)
  box(x, gy + 0.98, z, 0.17, 0.2, 0.02, rot)
end
do local x, z = local2world(0.95 * 1.0 + 0.01, 0.05); box(x, gy + 0.98, z, 0.02, 0.18, 0.16, rot) end
material{color={0.30, 0.18, 0.10}, roughness=0.6}
do local x, z = local2world(0, 0.71); box(x, gy + 0.62, z, 0.18, 0.36, 0.025, rot) end

-- ---------------------------------------------------------------- lantern posts and the cable between them
local posts = {}
local path = {{1.8, 2.2}, {1.0, 1.7}, {0.0, 1.3}, {-1.6, 2.2}}
material{color={0.12, 0.10, 0.09}, roughness=0.4, metallic=0.9}
for i, p in ipairs(path) do
  local y = groundY(p[1], p[2])
  frustum(p[1], y, p[2], 0.045, y + 1.15, 0.035, 10)
  posts[i] = {p[1], y + 1.12, p[2]}
end
for i = 1, #posts - 1 do      -- sagging cable swept with Leorix's curve tube
  local a, b = posts[i], posts[i + 1]
  local pts = {}
  for k = 0, 6 do
    local t = k / 6
    pts[#pts + 1] = {a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t - 0.28 * sin(PI * t), a[3] + (b[3] - a[3]) * t}
  end
  curve(pts, {radius=0.012, sides=1})
end
-- glowing lanterns on the posts and hanging from the cables
material{color={1, 0.85, 0.55}, emission={1.0, 0.64, 0.28}, strength=40, smooth=true}
for _, p in ipairs(posts) do ball(p[1], p[2] + 0.08, p[3], 0.09, 16) end
for i = 1, #posts - 1 do
  local a, b = posts[i], posts[i + 1]
  for _, t in ipairs({0.33, 0.66}) do
    ball(a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t - 0.28 * sin(PI * t) - 0.07, a[3] + (b[3] - a[3]) * t, 0.045, 12)
  end
end

-- ---------------------------------------------------------------- the stone path: flat cobbles
material{color={0.62, 0.58, 0.52}, roughness=0.8, smooth=true}
for k = 0, 22 do
  local t = k / 22
  local x = 1.9 * (1 - t) + 0.15 * t + 0.25 * sin(t * 5)
  local z = 3.4 * (1 - t) + 0.05 * t
  local px, pz = x + rr(-0.12, 0.12), z + rr(-0.1, 0.1)
  blob(px, groundY(px, pz) + 0.01, pz, rr(0.11, 0.17), 0.035, rr(0.1, 0.15), 10)
end

-- ---------------------------------------------------------------- trees: round broadleaf, tall pines and one in blossom
local function trunk(x, y, z, s)
  material{color={0.26, 0.17, 0.11}, roughness=0.9}
  frustum(x, y - 0.05, z, 0.09 * s, y + 1.1 * s, 0.05 * s, 8)
end
local function roundTree(x, z, s, tint, puffs)
  local y = groundY(x, z)
  trunk(x, y, z, s)
  for k = 1, puffs do
    local ox, oy, oz = rr(-0.38, 0.38) * s, (1.05 + rr(0, 0.75)) * s, rr(-0.38, 0.38) * s
    local r = rr(0.3, 0.55) * s
    local shade = rr(0.85, 1.15)          -- each puff a slightly different green
    material{color={tint[1] * shade, tint[2] * shade, tint[3] * shade}, roughness=0.8, smooth=true}
    blob(x + ox, y + oy, z + oz, r, r * 0.88, r, 20, function(dx, dy, dz)
      return 0.82 + 0.34 * fbm(dx * 2.4 + x, dy * 2.4 + k, dz * 2.4 + z, 3) end)
  end
end
local function pine(x, z, s, tint)
  local y = groundY(x, z)
  trunk(x, y, z, s * 0.8)
  material{color=tint, roughness=0.85}
  for k = 0, 3 do
    local base = y + (0.45 + k * 0.42) * s
    frustum(x, base, z, (0.62 - k * 0.12) * s, base + 0.62 * s, 0.0, 12)
  end
end
roundTree(-2.6, -0.2, 1.15, {0.20, 0.40, 0.13}, 5)
roundTree(-2.1, 1.5, 0.9, {0.27, 0.45, 0.15}, 4)
roundTree(2.8, -0.6, 0.85, {0.30, 0.47, 0.16}, 4)
roundTree(-3.0, 0.7, 0.75, {0.80, 0.42, 0.13}, 4)              -- autumn accent
roundTree(0.6, -2.7, 0.8, {0.95, 0.62, 0.72}, 5)               -- blossom
pine(1.7, -2.0, 1.3, {0.12, 0.30, 0.17})
pine(2.4, -1.4, 0.95, {0.14, 0.33, 0.19})
pine(-1.1, -3.0, 1.15, {0.11, 0.28, 0.16})
pine(-3.3, -1.4, 0.9, {0.13, 0.31, 0.18})

-- ---------------------------------------------------------------- the glass orb on a stone plinth (secondary focal point)
local ox, oz = 0.9, 0.2
local oy = groundY(ox, oz)
material{color={0.58, 0.55, 0.50}, roughness=0.75}
frustum(ox, oy - 0.05, oz, 0.26, oy + 0.18, 0.22, 16)
frustum(ox, oy + 0.18, oz, 0.13, oy + 0.62, 0.11, 16)
frustum(ox, oy + 0.62, oz, 0.2, oy + 0.7, 0.2, 16)
material{color={0.92, 0.97, 1.0}, roughness=0.02, glass=1.5, emission={0.2, 0.55, 0.75}, strength=0.6, smooth=true, sample_light=false}
ball(ox, oy + 0.98, oz, 0.28, 48)
-- a tiny glowing seed inside the orb
material{color={0.6, 0.9, 1}, emission={0.35, 0.8, 1.0}, strength=6, smooth=true}
ball(ox, oy + 0.98, oz, 0.06, 16)

-- ---------------------------------------------------------------- crates and barrels by the door
material{color={0.55, 0.38, 0.22}, roughness=0.75}
do
  local x, z = local2world(1.25, 0.85)
  box(x, groundY(x, z) + 0.17, z, 0.17, 0.17, 0.17, 0.2)
  box(x + 0.32, groundY(x, z) + 0.15, z + 0.12, 0.15, 0.15, 0.15, -0.35)
  box(x + 0.12, groundY(x, z) + 0.47, z + 0.04, 0.13, 0.13, 0.13, 0.6)
end
material{color={0.40, 0.25, 0.14}, roughness=0.7}
do
  local x, z = local2world(-1.25, 0.85)
  frustum(x, groundY(x, z), z, 0.15, groundY(x, z) + 0.42, 0.15, 14)
end
material{color={0.55, 0.52, 0.48}, roughness=0.4, metallic=1.0}
do
  local x, z = local2world(-1.25, 0.85)
  local y = groundY(x, z)
  frustum(x, y + 0.08, z, 0.155, y + 0.11, 0.155, 14)
  frustum(x, y + 0.31, z, 0.155, y + 0.34, 0.155, 14)
end

-- ---------------------------------------------------------------- rocks and flowers scattered on the grass
material{color={0.50, 0.47, 0.44}, roughness=0.85, smooth=true}
for _ = 1, 14 do
  local a, d = rr(0, 2 * PI), rr(1.8, 3.6)
  local x, z = cos(a) * d, sin(a) * d
  local s = rr(0.08, 0.22)
  blob(x, groundY(x, z) + s * 0.3, z, s, s * 0.7, s * 1.2, 12, function(dx, dy, dz)
    return 0.75 + 0.5 * fbm(dx * 3 + x, dy * 3, dz * 3 + z, 2) end)
end
-- flowers: little clusters in strong colours, each on a short stem
local petals = {{0.95, 0.80, 0.20}, {0.92, 0.25, 0.35}, {0.62, 0.35, 0.90}, {0.98, 0.95, 0.92}, {0.98, 0.50, 0.20}}
for i = 1, #petals do
  for _ = 1, 9 do
    local a, d = rr(0, 2 * PI), rr(0.9, 3.7)
    local cx, cz = cos(a) * d, sin(a) * d
    for _ = 1, 5 do
      local x, z = cx + rr(-0.22, 0.22), cz + rr(-0.22, 0.22)
      local y = groundY(x, z)
      local h = rr(0.08, 0.16)
      material{color={0.20, 0.40, 0.14}, roughness=0.8}
      frustum(x, y, z, 0.006, y + h, 0.004, 4)
      material{color=petals[i], roughness=0.55, smooth=true}
      ball(x, y + h + 0.02, z, rr(0.028, 0.042), 8)
    end
  end
end

-- grass: darker and yellower patches over the lawn, and tufts of blades
local patches = {{0.20, 0.36, 0.12}, {0.36, 0.48, 0.16}, {0.24, 0.42, 0.20}}
for i = 1, 3 do
  material{color=patches[i], roughness=0.9, smooth=true}
  for _ = 1, 10 do
    local a, d = rr(0, 2 * PI), rr(0.5, 3.4)
    local x, z = cos(a) * d, sin(a) * d
    local r = rr(0.35, 0.8)
    local m = mesh()
    local c = m:vert(x, groundY(x, z) + 0.012, z)
    local ring = {}
    for k = 0, 13 do
      local t = 2 * PI * k / 14
      local rk = r * (0.7 + 0.5 * fbm(cos(t) * 2 + x, 0, sin(t) * 2 + z, 2))
      local px, pz = x + cos(t) * rk, z + sin(t) * rk
      ring[k] = m:vert(px, groundY(px, pz) + 0.008, pz)
    end
    for k = 0, 13 do m:face(c, ring[(k + 1) % 14], ring[k]) end
    m:build()
  end
end
for t = 1, 3 do
  material{color={0.22 + 0.08 * t, 0.42 + 0.04 * t, 0.13}, roughness=0.8}
  local m = mesh()
  for _ = 1, 90 do
    local a, d = rr(0, 2 * PI), rr(0.4, 3.9)
    local x, z = cos(a) * d, sin(a) * d
    local y = groundY(x, z)
    for _ = 1, 6 do                         -- each blade a thin leaning triangle
      local ba = rr(0, 2 * PI)
      local bx, bz = x + cos(ba) * 0.03, z + sin(ba) * 0.03
      local h, lean = rr(0.08, 0.2), rr(0.02, 0.06)
      local w = 0.012
      local a1 = m:vert(bx - sin(ba) * w, y, bz + cos(ba) * w)
      local a2 = m:vert(bx + sin(ba) * w, y, bz - cos(ba) * w)
      local tip = m:vert(bx + cos(ba) * lean, y + h, bz + sin(ba) * lean)
      m:face(a1, a2, tip)
      m:face(a2, a1, tip)
    end
  end
  m:build()
end

-- ---------------------------------------------------------------- a waterfall pouring off the right edge
do
  local a = 0.12                       -- angle on the rim
  local nx, nz = cos(a), sin(a)        -- outward
  local tx, tz = -nz, nx               -- along the rim
  local er = edgeR(a) * 0.98
  local ex, ez = nx * er, nz * er
  local top = groundY(ex, ez) - 0.02
  -- the curtain: wide at the lip, narrowing as it falls, pale and faintly glowing
  material{color={0.88, 0.95, 1.0}, roughness=0.2, emission={0.55, 0.75, 0.95}, strength=0.5, smooth=true, sample_light=false}
  local m = mesh()
  local cols, rows = 10, 30
  local v = {}
  for r = 0, rows do
    local t = r / rows
    v[r] = {}
    local out = 0.6 * sqrt(t) + 0.25 * t
    local half = 0.55 * (1 - 0.6 * t)
    for c = 0, cols do
      local u = c / cols * 2 - 1
      local wob = 0.06 * (fbm(u * 3, t * 7, 1, 2) - 0.5)
      v[r][c] = m:vert(ex + nx * (out + wob) + tx * u * half, top - 4.6 * t * t - 0.08 * u * u,
                       ez + nz * (out + wob) + tz * u * half)
    end
  end
  for r = 0, rows - 1 do
    for c = 0, cols - 1 do m:face(v[r][c], v[r][c + 1], v[r + 1][c + 1], v[r + 1][c]) end
  end
  m:build()
  -- the stream feeding it
  material{color={0.45, 0.62, 0.78}, roughness=0.06, metallic=0.3, smooth=true}
  local pts = {}
  for k = 0, 5 do
    local t = k / 5
    local d = er * (0.55 + 0.45 * t)
    local aa = a + 0.25 * (1 - t) * sin(t * 3)
    local px, pz = cos(aa) * d, sin(aa) * d
    pts[#pts + 1] = {px, groundY(px, pz) + 0.005, pz}
  end
  curve(pts, {radius=0.13, sides=2})
  -- white foam at the lip and spray where it breaks up below
  material{color={0.95, 0.97, 1}, roughness=1, emission={0.6, 0.7, 0.85}, strength=0.35, smooth=true, sample_light=false}
  for k = 1, 7 do
    local u = rr(-1, 1)
    ball(ex + nx * 0.05 + tx * u * 0.5, top + 0.02, ez + nz * 0.05 + tz * u * 0.5, rr(0.05, 0.09), 10)
  end
  local bx, by, bz = ex + nx * 0.85, top - 4.6, ez + nz * 0.85
  for k = 1, 18 do
    local u, s = rr(-1, 1), rr(0.12, 0.3)
    blob(bx + tx * u * 0.4 + nx * rr(-0.2, 0.3), by + rr(-0.25, 0.25), bz + tz * u * 0.4 + nz * rr(-0.2, 0.3),
         s, s * 0.8, s, 12, function(dx, dy, dz) return 0.8 + 0.4 * fbm(dx * 3 + k, dy * 3, dz * 3, 2) end)
  end
end

-- ---------------------------------------------------------------- the underside: glowing crystals and hanging roots
local function undersideAt(a, t)       -- a point on the rock underside at angle a, depth t (0 rim .. 1 tip)
  local base = edgeR(a)
  local rad = base * (1 - t) ^ 1.35 * 0.92
  local x0, z0 = cos(a), sin(a)
  return x0 * rad, groundY(x0 * base, z0 * base) - 0.02 - t * 5.2, z0 * rad
end
local function crystal(x, y, z, dx, dy, dz, len, w)
  -- a six-sided spike from (x, y, z) along (dx, dy, dz)
  local l = sqrt(dx * dx + dy * dy + dz * dz); dx, dy, dz = dx / l, dy / l, dz / l
  local ux, uy, uz = 0, 1, 0
  if abs(dy) > 0.9 then ux, uy, uz = 1, 0, 0 end
  local px, py, pz = dy * uz - dz * uy, dz * ux - dx * uz, dx * uy - dy * ux
  local pl = sqrt(px * px + py * py + pz * pz); px, py, pz = px / pl, py / pl, pz / pl
  local qx, qy, qz = dy * pz - dz * py, dz * px - dx * pz, dx * py - dy * px
  local m = mesh()
  local ring = {}
  for k = 0, 5 do
    local t = 2 * PI * k / 6
    local c, sn = cos(t) * w, sin(t) * w
    local mx, my, mz = x + dx * len * 0.25, y + dy * len * 0.25, z + dz * len * 0.25
    ring[k] = m:vert(mx + px * c + qx * sn, my + py * c + qy * sn, mz + pz * c + qz * sn)
  end
  local root = m:vert(x - dx * 0.05, y - dy * 0.05, z - dz * 0.05)
  local tip = m:vert(x + dx * len, y + dy * len, z + dz * len)
  for k = 0, 5 do
    local k2 = (k + 1) % 6
    m:face(ring[k], ring[k2], tip)
    m:face(ring[k2], ring[k], root)
  end
  m:build()
end
local glows = {{0.35, 0.85, 1.0}, {0.70, 0.45, 1.0}}
for g = 1, 2 do
  material{color=glows[g], roughness=0.15, emission=glows[g], strength=7}
  for _ = 1, 5 do
    local a, t = rr(-1.2, 2.4), rr(0.15, 0.55)
    local x, y, z = undersideAt(a, t)
    for _ = 1, 4 do
      local ox, oz = rr(-0.15, 0.15), rr(-0.15, 0.15)
      crystal(x + ox, y + rr(-0.1, 0.1), z + oz, cos(a) + rr(-0.4, 0.4), rr(-1.2, -0.3), sin(a) + rr(-0.4, 0.4),
              rr(0.25, 0.6), rr(0.04, 0.08))
    end
  end
end
material{color={0.22, 0.15, 0.10}, roughness=0.9}
for _ = 1, 22 do
  local a, t = rr(0, 2 * PI), rr(0.03, 0.3)
  local x, y, z = undersideAt(a, t)
  local pts, taper = {}, {}
  local len, sway = rr(0.8, 2.6), rr(0, 2 * PI)
  for k = 0, 6 do
    local f = k / 6
    pts[#pts + 1] = {x + cos(a) * 0.15 * f + 0.12 * sin(f * 5 + sway), y - len * f, z + sin(a) * 0.15 * f + 0.12 * cos(f * 4 + sway)}
    taper[#taper + 1] = 1 - 0.85 * f
  end
  curve(pts, {radius=rr(0.018, 0.035), sides=1, taper=taper})
end
material{color={0.20, 0.42, 0.16}, roughness=0.85}
for _ = 1, 14 do                          -- a few green vines with leaves at the rim
  local a = rr(0, 2 * PI)
  local x, y, z = undersideAt(a, 0.02)
  local pts = {}
  local len = rr(0.5, 1.5)
  for k = 0, 5 do
    local f = k / 5
    pts[#pts + 1] = {x + cos(a) * 0.1 * f, y - len * f, z + sin(a) * 0.1 * f + 0.05 * sin(f * 6)}
  end
  curve(pts, {radius=0.012, sides=1})
  for k = 1, 4 do
    local p = pts[k + 1]
    blob(p[1] + rr(-0.04, 0.04), p[2], p[3] + rr(-0.04, 0.04), 0.05, 0.02, 0.035, 8)
  end
end

-- ---------------------------------------------------------------- cumulus far below, lit from the sunset side
for k = 1, 64 do
  local a, d = rr(0, 2 * PI), 45 + 120 * sqrt(rand())
  local cx, cz = cos(a) * d, sin(a) * d
  local cy = -42 + rr(-3, 3)
  local size = rr(5.0, 11.0)
  local lit = 0.5 + 0.5 * max(0, (-cos(a) * 0.93 - sin(a) * 0.36))   -- facing the sun side
  for j = 1, 9 do                         -- a heap of puffs: big at the base, smaller on top
    local ox, oz = rr(-1.6, 1.6) * size, rr(-1.0, 1.0) * size
    local up = rr(0, 1)
    local r = size * (0.75 - 0.35 * up) * rr(0.7, 1.1)
    local tone = 0.55 + 0.45 * up
    -- the glow fakes sunset light scattering through the cloud; too dim and far to be worth light-sampling
    material{color={0.62 * tone + 0.3 * lit * up, 0.56 * tone + 0.1 * lit * up, 0.72 * tone}, roughness=1, smooth=true,
             emission={0.30 + 0.45 * lit * up, 0.18 + 0.14 * lit * up, 0.32}, strength=0.24, sample_light=false}
    blob(cx + ox, cy + up * size * 0.9, cz + oz, r, r * 0.8, r, 12, function(dx, dy, dz)
      local k2 = 0.82 + 0.3 * fbm(dx * 2 + cx, dy * 2 + j, dz * 2 + cz, 2)
      if dy < -0.2 then k2 = k2 * (1 + dy * 0.5) end   -- flatter bottoms
      return k2 end)
  end
end

-- ---------------------------------------------------------------- little floating rocks around the island
material{color={0.42, 0.33, 0.26}, roughness=0.9, smooth=true}
local floaters = {{-5.8, -1.0, 2.2, 0.55}, {5.6, -0.6, -2.6, 0.42}, {-3.6, 1.6, -5.2, 0.32}, {6.2, 1.2, 0.8, 0.22}, {-6.6, -3.6, -1.6, 0.45}, {3.2, -3.4, 5.4, 0.3}}
for i, f in ipairs(floaters) do
  local x, y, z, s = f[1], f[2], f[3], f[4]
  blob(x, y, z, s, s * 0.9, s, 18, function(dx, dy, dz)
    local k = 0.8 + 0.35 * fbm(dx * 2 + i, dy * 2, dz * 2 - i, 3)
    if dy > 0.3 then k = k * (1 - 0.6 * (dy - 0.3)) end   -- flatter tops
    if dy < 0 then k = k * (1 + 0.7 * -dy) end            -- pointy bottoms
    return k end)
  material{color={0.28, 0.46, 0.17}, roughness=0.85, smooth=true}
  blob(x, y + s * 0.62, z, s * 0.85, s * 0.12, s * 0.85, 14)
  material{color={0.42, 0.33, 0.26}, roughness=0.9, smooth=true}
end

-- ---------------------------------------------------------------- fireflies drifting over the grass
material{color={1, 0.95, 0.6}, emission={1.0, 0.85, 0.35}, strength=40, smooth=true}
for _ = 1, 26 do
  local a, d = rr(0, 2 * PI), rr(2.2, 4.2)       -- out over the lawn and the rim, clear of the cottage walls
  local x, z = cos(a) * d, sin(a) * d
  ball(x, groundY(x, z) + rr(0.3, 1.6), z, rr(0.016, 0.024), 6)
end

-- ---------------------------------------------------------------- camera and light
-- Dusk: the sun has just set behind the island, on the left. A warm low key light rims the roof,
-- trees and rock edge; the sky does very little fill, so the windows and lanterns pool the light.
camera{eye={10.8, 0.9, 11.6}, target={-1.1, 0.1, 1.3}, lens=30, fstop=6, focus=15.2}
world{preset="Dusk", strength=0.55, rotation=159, sun_elevation=7, sun_azimuth=-111, sun_strength=2.6,
      sun_color={1.0, 0.55, 0.28}, exposure=0.9}
