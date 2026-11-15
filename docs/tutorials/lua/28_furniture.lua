-- A furniture generator: a table and chairs built from a few parameters.

local m = mesh()

-- Step 1: One turned block is enough for furniture
local function block(cx, cy, cz, sx, sy, sz, yaw)
  local c, s = math.cos(yaw or 0), math.sin(yaw or 0)
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

-- Step 2: Local coordinates: describe a part relative to the piece, then turn and move it
local function part(px, pz, yaw, lx, ly, lz, sx, sy, sz)
  local c, s = math.cos(yaw), math.sin(yaw)
  block(px + lx * c - lz * s, ly, pz + lx * s + lz * c, sx, sy, sz, yaw)
end

-- Step 3: A chair from a table of settings
local function chair(px, pz, yaw, o)
  local W, D, SEAT, LEG, BACK = o.width, o.depth, o.seat_h, o.leg, o.back_h
  local hx, hz = W / 2 - LEG / 2, D / 2 - LEG / 2
  for _, p in ipairs({{-hx, -hz}, {hx, -hz}, {-hx, hz}, {hx, hz}}) do
    part(px, pz, yaw, p[1], SEAT / 2, p[2], LEG, SEAT, LEG)        -- legs
  end
  part(px, pz, yaw, 0, SEAT + 0.025, 0, W, 0.05, D)                  -- seat
  part(px, pz, yaw, -hx, SEAT + BACK / 2, hz, LEG, BACK, LEG)        -- back posts
  part(px, pz, yaw, hx, SEAT + BACK / 2, hz, LEG, BACK, LEG)
  for k = 1, o.slats do                                              -- back slats
    part(px, pz, yaw, 0, SEAT + BACK * k / (o.slats + 1), hz, W - LEG, 0.04, 0.025)
  end
  part(px, pz, yaw, 0, SEAT + BACK - 0.03, hz, W, 0.06, 0.05)        -- top rail
end

-- Step 4: A table, the same way
local function table_(px, pz, o)
  local hx, hz = o.length / 2 - 0.08, o.width / 2 - 0.08
  for _, p in ipairs({{-hx, -hz}, {hx, -hz}, {-hx, hz}, {hx, hz}}) do
    part(px, pz, 0, p[1], o.height / 2, p[2], 0.07, o.height, 0.07)
  end
  part(px, pz, 0, 0, o.height - 0.06, -hz, o.length - 0.16, 0.08, 0.03)   -- aprons
  part(px, pz, 0, 0, o.height - 0.06, hz, o.length - 0.16, 0.08, 0.03)
  part(px, pz, 0, 0, o.height + 0.02, 0, o.length, 0.04, o.width)         -- top
end

-- Step 5: Arrange a dining set: chairs face the table from both sides and the ends
local TABLE = {length = 1.8, width = 0.9, height = 0.75}
local C = {width = 0.45, depth = 0.45, seat_h = 0.46, leg = 0.04, back_h = 0.48, slats = 3}
table_(0, 0, TABLE)
for _, x in ipairs({-0.55, 0, 0.55}) do
  -- a chair's back is on its local +z side; turn it so the back points away from the table
  chair(x, -0.62, PI, C)               -- local +z becomes world -z
  chair(x, 0.62, 0, C)
end
chair(-1.12, 0, PI / 2, C)             -- local +z becomes world -x
chair(1.12, 0, -PI / 2, C)

m:build()
