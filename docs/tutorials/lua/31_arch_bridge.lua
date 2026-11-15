-- An arched stone bridge with a fenced walkway: arches from blocks placed along a curve.

local m = mesh()
local SPAN, RISE, WIDTH = 6.0, 1.4, 1.1 -- length, arch height, deck width
local STONES, POSTS = 21, 13

local function block(cx, cy, cz, sx, sy, sz, roll)    -- roll: turn around the Z axis
  local c, s = math.cos(roll or 0), math.sin(roll or 0)
  local v = {}
  for i = 0, 7 do
    local x = ((i & 1 == 0) and -0.5 or 0.5) * sx
    local y = ((i & 2 == 0) and -0.5 or 0.5) * sy
    local z = ((i & 4 == 0) and -0.5 or 0.5) * sz
    v[i] = m:vert(cx + x * c - y * s, cy + x * s + y * c, cz + z)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end

-- Step 1: The arch curve: a parabola that is 0 at both ends and RISE in the middle
local function arch_y(x) return RISE * (1 - (2 * x / SPAN) ^ 2) end
local function arch_slope(x) return -8 * RISE * x / (SPAN * SPAN) end

-- Step 2: Arch stones (voussoirs): blocks placed along the curve and turned to follow it
for i = 0, STONES - 1 do
  local x = (i + 0.5) / STONES * SPAN - SPAN / 2
  local angle = math.atan(arch_slope(x))
  local len = SPAN / STONES / math.cos(angle) * 0.96         -- 4% gap shows the joints
  block(x, arch_y(x) - 0.12, 0, len, 0.24, WIDTH, angle)
end

-- Step 3: The deck: a strip that follows a flatter curve above the arch
local function deck_y(x) return 0.25 + 0.75 * arch_y(x) + 0.25 end
local N = 30
for i = 0, N - 1 do
  local x0 = i / N * (SPAN + 1.2) - (SPAN + 1.2) / 2
  local x1 = (i + 1) / N * (SPAN + 1.2) - (SPAN + 1.2) / 2
  local xm = (x0 + x1) / 2
  local y0 = deck_y(math.max(-SPAN / 2, math.min(SPAN / 2, xm)))
  local angle = math.atan((deck_y(math.max(-SPAN / 2, math.min(SPAN / 2, x1))) - deck_y(math.max(-SPAN / 2, math.min(SPAN / 2, x0)))) / (x1 - x0))
  block(xm, y0, 0, (x1 - x0) * 1.02 / math.cos(angle), 0.12, WIDTH + 0.1, angle)
end

-- Step 4: Spandrel piers fill the gap between arch and deck
for _, x in ipairs({-2.4, -1.6, 1.6, 2.4}) do
  local top, bottom = deck_y(x), arch_y(x)
  block(x, (top + bottom) / 2, 0, 0.18, top - bottom, WIDTH * 0.9, 0)
end
block(-SPAN / 2 - 0.2, 0.25, 0, 0.6, 0.5, WIDTH + 0.2, 0)     -- abutments
block(SPAN / 2 + 0.2, 0.25, 0, 0.6, 0.5, WIDTH + 0.2, 0)

-- Step 5: A fence on both sides: posts plus two rails that follow the deck
for _, z in ipairs({-WIDTH / 2, WIDTH / 2}) do
  for p = 0, POSTS - 1 do
    local x = p / (POSTS - 1) * SPAN - SPAN / 2
    local y = deck_y(x)
    block(x, y + 0.3, z, 0.06, 0.5, 0.06, 0)
  end
  for _, h in ipairs({0.25, 0.5}) do
    for p = 0, POSTS - 2 do
      local x0 = p / (POSTS - 1) * SPAN - SPAN / 2
      local x1 = (p + 1) / (POSTS - 1) * SPAN - SPAN / 2
      local y0, y1 = deck_y(x0) + h, deck_y(x1) + h
      local angle = math.atan((y1 - y0) / (x1 - x0))
      block((x0 + x1) / 2, (y0 + y1) / 2, z, (x1 - x0) / math.cos(angle), 0.04, 0.04, angle)
    end
  end
end

m:build()
