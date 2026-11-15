-- A growing tree, one frame at a time.
-- Leorix scripts build meshes, not animations: to animate, run the script once per frame
-- with a different time value t, then keyframe the frames in the Timeline.

-- Step 1: One number drives everything: t goes from 0 (seed) to 1 (fully grown)
local t = T or 1.0                     -- change this value for each frame you make
local LEVELS, SEED = 6, 4

local m = mesh()

-- Step 2: Turn t into "how far along" each branch level is
-- growth runs from 0 to LEVELS + 1; level d grows while growth is between d and d + 1
local growth = t * (LEVELS + 1)
local function amount(d) return math.max(0, math.min(1, growth - d)) end

-- Step 3: The usual helpers: a capped, tapered branch and a leaf cluster
local function sub(a, b) return {a[1] - b[1], a[2] - b[2], a[3] - b[3]} end
local function cross(a, b) return {a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1]} end
local function norm(a) local l = math.sqrt(a[1] ^ 2 + a[2] ^ 2 + a[3] ^ 2); return {a[1] / l, a[2] / l, a[3] / l} end
local function branch(p, d, len, r0, r1)
  local side = math.abs(d[2]) < 0.9 and {0, 1, 0} or {1, 0, 0}
  local u = norm(cross(d, side))
  local w = cross(d, u)
  local q = {p[1] + d[1] * len, p[2] + d[2] * len, p[3] + d[3] * len}
  local a, b = {}, {}
  for i = 1, 6 do
    local ang = (i - 1) / 6 * 2 * PI
    local c, s = math.cos(ang), math.sin(ang)
    local o = {u[1] * c + w[1] * s, u[2] * c + w[2] * s, u[3] * c + w[3] * s}
    a[i] = m:vert(p[1] + o[1] * r0, p[2] + o[2] * r0, p[3] + o[3] * r0)
    b[i] = m:vert(q[1] + o[1] * r1, q[2] + o[2] * r1, q[3] + o[3] * r1)
  end
  for i = 1, 6 do local j = i % 6 + 1; m:face(a[i], a[j], b[j], b[i]) end
  local near = {}
  for i = 6, 1, -1 do near[#near + 1] = a[i] end
  m:face(table.unpack(near)); m:face(table.unpack(b))
  return q
end
local function leaf(p, s)
  local v = {m:vert(p[1] + s, p[2], p[3]), m:vert(p[1] - s, p[2], p[3]), m:vert(p[1], p[2] + s, p[3]),
             m:vert(p[1], p[2] - s, p[3]), m:vert(p[1], p[2], p[3] + s), m:vert(p[1], p[2], p[3] - s)}
  local F = {{1, 3, 5}, {5, 3, 2}, {2, 3, 6}, {6, 3, 1}, {5, 4, 1}, {2, 4, 5}, {6, 4, 2}, {1, 4, 6}}
  for _, f in ipairs(F) do m:face(v[f[1]], v[f[2]], v[f[3]]) end
end

-- Step 4: Recursive growth: each level only appears once its turn comes
local function grow(p, yaw, pitch, depth)
  local a = amount(depth)
  if a <= 0 then return end
  local d = {math.sin(pitch) * math.cos(yaw), math.cos(pitch), math.sin(pitch) * math.sin(yaw)}
  local len = 0.9 * 0.75 ^ depth * a
  local r = 0.1 * 0.66 ^ depth
  local q = branch(p, d, len, r * math.max(a, 0.3), r * 0.75 * math.max(a, 0.3))
  if depth == LEVELS - 1 then
    local la = amount(LEVELS)                     -- leaves come last
    if la > 0 then leaf(q, 0.13 * la) end
    return
  end
  for k = 0, 2 do
    local jitter = ((depth * 7 + k * 13 + SEED) % 5) * 0.12
    grow(q, yaw + k * 2 * PI / 3 + depth * 0.7 + jitter, 0.45 + depth * 0.1, depth + 1)
  end
end

-- Step 5: A little mound of soil, so even frame 0 has something to show
local ring = {}
for i = 1, 16 do
  local ang = (i - 1) / 16 * 2 * PI
  ring[i] = m:vert(math.cos(ang) * 0.6, 0, math.sin(ang) * 0.6)
end
local top = m:vert(0, 0.08, 0)
for i = 1, 16 do m:face(ring[i], top, ring[i % 16 + 1]) end
m:face(table.unpack(ring))

grow({0, 0.05, 0}, 0, 0, 0)
m:build()
print("t =", t)
