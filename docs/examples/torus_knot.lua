-- Torus knot: sweep a circle along a (p, q) knot curve.
-- Builds a moving frame (tangent, normal, binormal) at every sample of the curve.

local P, Q       = 2, 3      -- the classic trefoil
local SAMPLES    = 240       -- along the curve
local SIDES      = 16        -- around the tube
local TUBE       = 0.22      -- tube radius

local function knot(t)       -- t in 0 .. 2*PI
  local r = 1 + 0.45 * math.cos(Q * t)
  return r * math.cos(P * t), 0.45 * math.sin(Q * t), r * math.sin(P * t)
end

local function sub(a, b) return {a[1] - b[1], a[2] - b[2], a[3] - b[3]} end
local function cross(a, b)
  return {a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1]}
end
local function norm(a)
  local l = math.sqrt(a[1] ^ 2 + a[2] ^ 2 + a[3] ^ 2)
  return {a[1] / l, a[2] / l, a[3] / l}
end

local m = mesh()
local rings = {}
for i = 0, SAMPLES - 1 do
  local t  = i / SAMPLES * 2 * PI
  local dt = 2 * PI / SAMPLES
  local p  = {knot(t)}
  local tangent = norm(sub({knot(t + dt)}, {knot(t - dt)}))
  local normal  = norm(cross(tangent, {0, 1, 0}))
  local binorm  = cross(normal, tangent)
  rings[i] = {}
  for s = 0, SIDES - 1 do
    local a = s / SIDES * 2 * PI
    local c, d = math.cos(a) * TUBE, math.sin(a) * TUBE
    rings[i][s] = m:vert(p[1] + normal[1] * c + binorm[1] * d,
                         p[2] + normal[2] * c + binorm[2] * d,
                         p[3] + normal[3] * c + binorm[3] * d)
  end
end

for i = 0, SAMPLES - 1 do
  local n = (i + 1) % SAMPLES          -- wrap: the tube is closed
  for s = 0, SIDES - 1 do
    local k = (s + 1) % SIDES
    m:face(rings[i][s], rings[n][s], rings[n][k], rings[i][k])
  end
end

m:build()
