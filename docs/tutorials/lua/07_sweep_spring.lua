-- Sweeping a tube along a curve: a coil spring with straight legs.
-- Uses parallel-transport frames so the tube never twists or flips.

local m = mesh()
local TURNS, COIL_R, PITCH = 6, 0.8, 0.32
local TUBE_R, SIDES, SAMPLES = 0.07, 12, 360

-- Step 1: Sample the path as a list of points
local path = {}
path[#path + 1] = {COIL_R + 0.9, 0, 0}                       -- straight leg in
for i = 0, SAMPLES do
  local u = i / SAMPLES
  local a = u * TURNS * 2 * PI
  path[#path + 1] = {math.cos(a) * COIL_R, 0.15 + u * TURNS * PITCH, math.sin(a) * COIL_R}
end
local last = path[#path]
path[#path + 1] = {last[1], last[2] + 0.7, last[3]}           -- straight leg out (up)

-- Step 2: Small vector helpers
local function sub(a, b) return {a[1] - b[1], a[2] - b[2], a[3] - b[3]} end
local function dot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
local function cross(a, b) return {a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1]} end
local function norm(a) local l = math.sqrt(dot(a, a)); return {a[1] / l, a[2] / l, a[3] / l} end

-- Step 3: Tangents, then parallel-transport the normal along the path
local tangent, normal = {}, {}
for i = 1, #path do
  local a, b = path[math.max(1, i - 1)], path[math.min(#path, i + 1)]
  tangent[i] = norm(sub(b, a))
end
normal[1] = norm(cross(tangent[1], {0, 1, 0}))
for i = 2, #path do
  local n, t = normal[i - 1], tangent[i]
  local d = dot(n, t)
  normal[i] = norm({n[1] - t[1] * d, n[2] - t[2] * d, n[3] - t[3] * d})  -- remove the part along t
end

-- Step 4: Put a circle of vertices around every path point
local rings = {}
for i, p in ipairs(path) do
  local n, b = normal[i], cross(tangent[i], normal[i])
  local ring = {}
  for s = 1, SIDES do
    local a = (s - 1) / SIDES * 2 * PI
    local c, d = math.cos(a) * TUBE_R, math.sin(a) * TUBE_R
    ring[s] = m:vert(p[1] + n[1] * c + b[1] * d, p[2] + n[2] * c + b[2] * d, p[3] + n[3] * c + b[3] * d)
  end
  rings[i] = ring
end

-- Step 5: Stitch the rings and cap both ends
for i = 1, #rings - 1 do
  for s = 1, SIDES do
    local k = s % SIDES + 1
    m:face(rings[i][s], rings[i][k], rings[i + 1][k], rings[i + 1][s])
  end
end
local first, endcap = {}, {}
for s = SIDES, 1, -1 do first[#first + 1] = rings[1][s] end
for s = 1, SIDES do endcap[#endcap + 1] = rings[#rings][s] end
m:face(table.unpack(first))
m:face(table.unpack(endcap))

m:build()
print(#path, "path points")
