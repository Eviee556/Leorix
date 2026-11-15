-- Extruding a 2D outline: a bevelled star award on a round plinth.

local m = mesh()

-- Step 1: Describe the outline as a list of 2D points
local POINTS, OUTER, INNER = 5, 1.0, 0.45
local outline = {}
for i = 0, POINTS * 2 - 1 do
  local a = i / (POINTS * 2) * 2 * PI + PI / 2
  local r = (i % 2 == 0) and OUTER or INNER
  outline[#outline + 1] = {math.cos(a) * r, math.sin(a) * r}
end

-- Step 2: Copy the outline at several heights and sizes (the "layers")
-- Each layer: {height, scale}. Changing the scale between layers makes a bevel.
local layers = {{0.00, 1.00}, {0.25, 1.00}, {0.33, 0.88}}
local ZOFF = 0.9                       -- lift the star off the plinth
local rows = {}
for l, L in ipairs(layers) do
  rows[l] = {}
  for i, p in ipairs(outline) do
    -- the star stands upright: outline in X/Y, extruded along Z
    rows[l][i] = m:vert(p[1] * L[2], p[2] * L[2] + ZOFF + 0.6, L[1] - 0.15)
  end
end

-- Step 3: Walls: quads between neighbouring layers
local n = #outline
for l = 1, #layers - 1 do
  for i = 1, n do
    local j = i % n + 1
    m:face(rows[l][i], rows[l][j], rows[l + 1][j], rows[l + 1][i])
  end
end

-- Step 4: Caps for a concave shape: fan from a centre point
-- A star is concave, so one n-gon would be split wrongly. A centre vertex fixes it.
local function fan(row, z, flip)
  local c = m:vert(0, ZOFF + 0.6, z)
  for i = 1, n do
    local j = i % n + 1
    if flip then m:face(c, row[j], row[i]) else m:face(c, row[i], row[j]) end
  end
end
fan(rows[#layers], layers[#layers][1] - 0.15, false)   -- front
fan(rows[1], -0.15, true)                              -- back

-- Step 5: A round plinth and a post, using the same layer idea
local function disc(y0, y1, r, sides)
  local lo, hi = {}, {}
  for i = 1, sides do
    local a = (i - 1) / sides * 2 * PI
    lo[i] = m:vert(math.cos(a) * r, y0, math.sin(a) * r)
    hi[i] = m:vert(math.cos(a) * r, y1, math.sin(a) * r)
  end
  for i = 1, sides do
    local j = i % sides + 1
    m:face(lo[i], hi[i], hi[j], lo[j])
  end
  m:face(table.unpack(lo))
  local top = {}
  for i = sides, 1, -1 do top[#top + 1] = hi[i] end
  m:face(table.unpack(top))
end
disc(0.0, 0.18, 0.9, 40)
disc(0.18, 0.30, 0.7, 40)
disc(0.30, ZOFF + 0.25, 0.08, 12)

m:build()
