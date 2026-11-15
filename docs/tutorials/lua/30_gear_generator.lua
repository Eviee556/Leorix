-- A spoked gear generator: teeth on a rim, a hub, spokes and a bevelled look from rings.

local m = mesh()
local TEETH, MODULE, THICK = 32, 0.1, 0.22
local SPOKES = 5

-- Step 1: Rings of points at the same angles can be joined with quads, holes included
local function ring_at(angles, radius, y, cx, cz)
  local r = {}
  for i, a in ipairs(angles) do r[i] = m:vert(cx + math.cos(a) * radius(i, a), y, cz + math.sin(a) * radius(i, a)) end
  return r
end
local function join(a, b)              -- quads between two rings with the same number of points
  local n = #a
  for i = 1, n do
    local j = i % n + 1
    m:face(a[i], b[i], b[j], a[j])
  end
end

-- Step 2: The tooth outline as a radius per angle: four points per tooth
local R = MODULE * TEETH / 2
local ro, ri, rim = R + MODULE, R - 1.25 * MODULE, R - 3 * MODULE
local angles, radii = {}, {}
for k = 0, TEETH - 1 do
  local w = 2 * PI / TEETH
  for _, p in ipairs({{0.00, ri}, {0.15, ro}, {0.50, ro}, {0.65, ri}}) do
    angles[#angles + 1] = k * w + p[1] * w
    radii[#radii + 1] = p[2]
  end
end
local tooth = function(i) return radii[i] end
local const = function(r) return function() return r end end

-- Step 3: The rim: toothed outside, round inside, closed top and bottom
local ob, ot = ring_at(angles, tooth, 0, 0, 0), ring_at(angles, tooth, THICK, 0, 0)
local ib, it = ring_at(angles, const(rim), 0, 0, 0), ring_at(angles, const(rim), THICK, 0, 0)
join(ob, ot)                           -- outer wall with teeth
join(it, ib)                           -- inner wall (reversed: faces the centre)
join(ot, it)                           -- top
join(ib, ob)                           -- bottom

-- Step 4: A hub with an axle hole, slightly thicker than the rim
local HUB, HOLE, n = R * 0.28, R * 0.1, 40
local round = {}
for i = 1, n do round[i] = (i - 1) / n * 2 * PI end
local hb, ht = ring_at(round, const(HUB), -0.04, 0, 0), ring_at(round, const(HUB), THICK + 0.04, 0, 0)
local kb, kt = ring_at(round, const(HOLE), -0.04, 0, 0), ring_at(round, const(HOLE), THICK + 0.04, 0, 0)
join(hb, ht); join(kt, kb); join(ht, kt); join(kb, hb)

-- Step 5: Spokes: turned boxes that reach from the hub into the rim
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
for k = 0, SPOKES - 1 do
  local a = k / SPOKES * 2 * PI
  local mid = (HUB + rim) / 2
  block(math.cos(a) * mid, THICK / 2, math.sin(a) * mid, rim - HUB + 0.1, THICK * 0.7, 0.14, a)
end

m:build()
print(TEETH, "teeth")
