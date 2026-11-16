-- Lantern Isle: a floating island at golden hour, built entirely from this script.
-- Render it headless:  leorix --headless-render docs/showcase/lantern_isle.lua hero.png --width 1920 --height 1080 --spp 512
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
material{color={1, 0.8, 0.5}, emission={1.0, 0.62, 0.28}, strength=16}
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
material{color={1, 0.85, 0.55}, emission={1.0, 0.66, 0.30}, strength=24, smooth=true}
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

-- ---------------------------------------------------------------- trees: tapered trunks and puffy canopies
local function tree(x, z, s, tint)
  local y = groundY(x, z)
  material{color={0.30, 0.20, 0.12}, roughness=0.9}
  frustum(x, y - 0.05, z, 0.09 * s, y + 1.1 * s, 0.05 * s, 8)
  material{color=tint, roughness=0.8, smooth=true}
  for k = 1, 4 do
    local ox, oy, oz = rr(-0.35, 0.35) * s, (1.1 + rr(0, 0.65)) * s, rr(-0.35, 0.35) * s
    local r = rr(0.38, 0.55) * s
    blob(x + ox, y + oy, z + oz, r, r * 0.9, r, 18, function(dx, dy, dz)
      return 0.85 + 0.3 * fbm(dx * 2 + x, dy * 2 + k, dz * 2 + z, 3) end)
  end
end
tree(-2.6, -0.2, 1.15, {0.22, 0.42, 0.14})
tree(-2.1, 1.4, 0.9, {0.30, 0.48, 0.16})
tree(1.6, -2.1, 1.25, {0.20, 0.38, 0.13})
tree(2.8, -0.6, 0.85, {0.34, 0.50, 0.18})
tree(-1.0, -2.9, 0.95, {0.25, 0.44, 0.15})
-- one autumn tree for a colour accent
tree(-3.0, 0.7, 0.8, {0.78, 0.40, 0.12})

-- ---------------------------------------------------------------- the glass orb on a stone plinth (secondary focal point)
local ox, oz = 0.9, 0.2
local oy = groundY(ox, oz)
material{color={0.58, 0.55, 0.50}, roughness=0.75}
frustum(ox, oy - 0.05, oz, 0.26, oy + 0.18, 0.22, 16)
frustum(ox, oy + 0.18, oz, 0.13, oy + 0.62, 0.11, 16)
frustum(ox, oy + 0.62, oz, 0.2, oy + 0.7, 0.2, 16)
material{color={0.92, 0.97, 1.0}, roughness=0.02, glass=1.5, smooth=true}
ball(ox, oy + 0.98, oz, 0.28, 48)
-- a tiny glowing seed inside the orb
material{color={0.6, 0.9, 1}, emission={0.35, 0.8, 1.0}, strength=18, smooth=true}
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
local petals = {{0.95, 0.85, 0.35}, {0.95, 0.55, 0.65}, {0.98, 0.95, 0.92}}
for i = 1, 3 do
  material{color=petals[i], roughness=0.6, smooth=true}
  for _ = 1, 26 do
    local a, d = rr(0, 2 * PI), rr(0.8, 3.7)
    local x, z = cos(a) * d, sin(a) * d
    ball(x, groundY(x, z) + 0.04, z, 0.035, 6)
  end
end

-- ---------------------------------------------------------------- a waterfall curtain pouring off the right edge
material{color={0.62, 0.78, 0.92}, roughness=0.25, emission={0.25, 0.45, 0.65}, strength=0.18, smooth=true}
do
  local a = 0.12                       -- angle on the rim
  local nx, nz = cos(a), sin(a)        -- outward
  local tx, tz = -nz, nx               -- along the rim
  local er = edgeR(a) * 0.98
  local ex, ez = nx * er, nz * er
  local top = groundY(ex, ez) - 0.03
  local m = mesh()
  local cols, rows = 8, 24
  local v = {}
  for r = 0, rows do
    local t = r / rows
    v[r] = {}
    local out = 0.55 * sqrt(t) + 0.2 * t   -- arcs outward, then drops
    local half = 0.32 + 0.25 * t           -- widens as it falls
    for c = 0, cols do
      local u = c / cols * 2 - 1
      local wob = 0.05 * (fbm(u * 3, t * 6, 1, 2) - 0.5)
      local x = ex + nx * (out + wob) + tx * u * half
      local z = ez + nz * (out + wob) + tz * u * half
      v[r][c] = m:vert(x, top - 5.0 * t * t - 0.05 * u * u, z)
    end
  end
  for r = 0, rows - 1 do
    for c = 0, cols - 1 do m:face(v[r][c], v[r][c + 1], v[r + 1][c + 1], v[r + 1][c]) end
  end
  m:build()
  -- a narrow stream on the grass feeding it
  material{color={0.55, 0.75, 0.9}, roughness=0.08, metallic=0.2, smooth=true}
  local pts = {}
  for k = 0, 5 do
    local t = k / 5
    local d = er * (0.55 + 0.45 * t)
    local aa = a + 0.25 * (1 - t) * sin(t * 3)
    local px, pz = cos(aa) * d, sin(aa) * d
    pts[#pts + 1] = {px, groundY(px, pz) + 0.005, pz}
  end
  curve(pts, {radius=0.11, sides=2})
  -- mist where it breaks up below
  material{color={0.70, 0.76, 0.88}, roughness=1}
  for k = 1, 9 do
    local u = rr(-1, 1)
    blob(ex + nx * 0.9 + tx * u * 0.5, top - 5.1 + rr(-0.2, 0.2), ez + nz * 0.9 + tz * u * 0.5,
         rr(0.18, 0.32), rr(0.12, 0.2), rr(0.18, 0.32), 10)
  end
end

-- ---------------------------------------------------------------- a sea of clouds far below
material{color={0.34, 0.40, 0.58}, roughness=1, smooth=true}
for k = 1, 140 do
  local a, d = rr(0, 2 * PI), 22 + 70 * sqrt(rand())
  local x, z = cos(a) * d, sin(a) * d
  local s = rr(4.0, 9.0)
  blob(x, -26 + rr(-1.5, 1.5), z, s * 1.6, s * 0.32, s, 14, function(dx, dy, dz)
    return 0.8 + 0.4 * fbm(dx * 1.5 + x, dy * 1.5, dz * 1.5 + z, 3) end)
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
for _ = 1, 46 do
  local a, d = rr(0, 2 * PI), rr(0.6, 3.9)
  local x, z = cos(a) * d, sin(a) * d
  ball(x, groundY(x, z) + rr(0.25, 1.9), z, rr(0.012, 0.02), 6)
end

-- ---------------------------------------------------------------- camera and light
-- Night: cool moonlight from the upper left, a starry sky, and the cottage, lanterns and orb glowing warm.
camera{eye={11.4, 2.1, 10.7}, target={0.15, -0.7, 0.0}, lens=36, fstop=8, focus=15.6}
world{preset="Night", strength=4.0, rotation=40, sun_elevation=38, sun_azimuth=-35, sun_strength=0.55,
      sun_color={0.62, 0.72, 1.0}, exposure=1.5}
