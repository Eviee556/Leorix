-- A spiral staircase with wedge-shaped treads, balusters and a swept handrail.

local m = mesh()
local STEPS, TURN = 28, 1.25           -- treads, and full turns from bottom to top
local RISE, INNER, OUTER = 0.17, 0.22, 1.5
local THICK, RAIL_H = 0.05, 0.85

-- Step 1: A ring-of-vertices helper we can reuse for the column and the rail
local function ngon_prism(cx, cz, r, y0, y1, sides)
  local lo, hi = {}, {}
  for i = 1, sides do
    local a = (i - 1) / sides * 2 * PI
    lo[i] = m:vert(cx + math.cos(a) * r, y0, cz + math.sin(a) * r)
    hi[i] = m:vert(cx + math.cos(a) * r, y1, cz + math.sin(a) * r)
  end
  for i = 1, sides do
    local j = i % sides + 1
    m:face(lo[i], hi[i], hi[j], lo[j])
  end
  local top = {}
  for i = sides, 1, -1 do top[#top + 1] = hi[i] end
  m:face(table.unpack(top)); m:face(table.unpack(lo))
end

-- Step 2: The central column
ngon_prism(0, 0, INNER, 0, STEPS * RISE + RAIL_H, 20)

-- Step 3: Wedge treads: a slice of a ring between two angles
local SLICE = TURN * 2 * PI / STEPS
local function tread(a0, a1, y)
  local p = {}
  local corners = {{INNER, a0}, {OUTER, a0}, {OUTER, a1}, {INNER, a1}}
  for k, c in ipairs(corners) do
    p[k] = m:vert(math.cos(c[2]) * c[1], y - THICK, math.sin(c[2]) * c[1])
    p[k + 4] = m:vert(math.cos(c[2]) * c[1], y, math.sin(c[2]) * c[1])
  end
  m:face(p[5], p[8], p[7], p[6])                 -- top
  m:face(p[1], p[2], p[3], p[4])                 -- bottom
  m:face(p[1], p[5], p[6], p[2])                 -- leading edge
  m:face(p[3], p[7], p[8], p[4])                 -- trailing edge
  m:face(p[2], p[6], p[7], p[3])                 -- outer edge
  m:face(p[4], p[8], p[5], p[1])                 -- inner edge (hidden by the column)
end
for i = 0, STEPS - 1 do
  local a0 = i * SLICE
  tread(a0, a0 + SLICE * 1.08, (i + 1) * RISE)   -- 8% overlap so treads look continuous from below
end

-- Step 4: One baluster per tread
for i = 0, STEPS - 1 do
  local a = (i + 0.5) * SLICE
  local x, z = math.cos(a) * (OUTER - 0.08), math.sin(a) * (OUTER - 0.08)
  ngon_prism(x, z, 0.018, (i + 1) * RISE, (i + 1) * RISE + RAIL_H, 6)
end

-- Step 5: Sweep a handrail along the helix through the baluster tops
local RS, SIDES, rings = 6 * STEPS, 8, {}
for k = 0, RS do
  local u = k / RS
  local a = (0.5 + u * (STEPS - 1)) * SLICE
  local y = (1 + u * (STEPS - 1)) * RISE + RAIL_H
  local cx, cz = math.cos(a) * (OUTER - 0.08), math.sin(a) * (OUTER - 0.08)
  local ring = {}
  for s = 1, SIDES do
    local t = (s - 1) / SIDES * 2 * PI
    local out = math.cos(t) * 0.035                  -- radial offset
    ring[s] = m:vert(cx + math.cos(a) * out, y + math.sin(t) * 0.035, cz + math.sin(a) * out)
  end
  rings[k] = ring
end
for k = 0, RS - 1 do
  for s = 1, SIDES do
    local j = s % SIDES + 1
    m:face(rings[k][s], rings[k][j], rings[k + 1][j], rings[k + 1][s])
  end
end

m:build()
