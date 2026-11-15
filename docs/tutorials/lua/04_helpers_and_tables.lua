-- A polygon lab: a reusable toolkit of helper functions, driven by a table of settings.

local m = mesh()

-- Step 1: ring() makes a circle of vertices and returns their indices in a table
local function ring(cx, cz, y, radius, sides, turn)
  local r = {}
  for i = 1, sides do
    local a = (i - 1) / sides * 2 * PI + (turn or 0)
    r[i] = m:vert(cx + math.cos(a) * radius, y, cz + math.sin(a) * radius)
  end
  return r
end

-- Step 2: bridge() joins two rings with a strip of quads
-- Rings run counter-clockwise seen from below; `lower` sits under `upper`, so normals face outward.
local function bridge(lower, upper)
  local n = #lower
  for i = 1, n do
    local j = i % n + 1
    m:face(lower[i], upper[i], upper[j], lower[j])
  end
end

-- Step 3: cap() closes a ring with a single n-gon, facing up or down
local function cap(r, facing_up)
  local idx = {}
  if facing_up then
    for i = #r, 1, -1 do idx[#idx + 1] = r[i] end
  else
    for i = 1, #r do idx[#idx + 1] = r[i] end
  end
  m:face(table.unpack(idx))
end

-- Step 4: A prism is now three lines
local function prism(cx, cz, radius, height, sides, twist, taper)
  local bottom = ring(cx, cz, 0, radius, sides, 0)
  local top = ring(cx, cz, height, radius * taper, sides, twist)
  bridge(bottom, top)
  cap(bottom, false)
  cap(top, true)
end

-- Step 5: Describe the whole scene as a table of settings
local specs = {
  {sides = 3, height = 1.2, twist = 0.0, taper = 1.0},
  {sides = 4, height = 1.5, twist = 0.4, taper = 0.9},
  {sides = 5, height = 1.8, twist = 0.6, taper = 0.8},
  {sides = 6, height = 2.1, twist = 0.8, taper = 0.7},
  {sides = 8, height = 2.4, twist = 1.0, taper = 0.6},
  {sides = 12, height = 2.7, twist = 1.2, taper = 0.5},
}
for i, s in ipairs(specs) do
  prism((i - 3.5) * 1.3, 0, 0.5, s.height, s.sides, s.twist, s.taper)
end

m:build()
print(#specs, "prisms")
