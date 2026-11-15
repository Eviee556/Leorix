-- A rotating gear train: three gears that stay meshed at every time t.

-- Step 1: Time sets the angle of the first gear
local t = T or 0.0
local MODULE, THICK = 0.08, 0.2        -- tooth size, gear thickness
local TURN = 2 * PI / 8                -- the driver turns 1/8 of a revolution per loop

local m = mesh()

-- Step 2: A gear is an outline with teeth plus a round hole, joined top and bottom by quads
local function gear(cx, cz, teeth, angle, y)
  local R = MODULE * teeth / 2                    -- pitch radius
  local ro, ri, hole = R + MODULE, R - 1.25 * MODULE, R * 0.3
  local pts = {}
  for k = 0, teeth - 1 do                         -- four outline points per tooth
    local base = angle + k * 2 * PI / teeth
    local w = 2 * PI / teeth
    for _, p in ipairs({{0.00, ri}, {0.18, ro}, {0.50, ro}, {0.68, ri}}) do
      pts[#pts + 1] = {base + p[1] * w, p[2]}
    end
  end
  local n = #pts
  local top_o, bot_o, top_i, bot_i = {}, {}, {}, {}
  for i, p in ipairs(pts) do
    local x, z = cx + math.cos(p[1]) * p[2], cz + math.sin(p[1]) * p[2]
    bot_o[i], top_o[i] = m:vert(x, y, z), m:vert(x, y + THICK, z)
    local hx, hz = cx + math.cos(p[1]) * hole, cz + math.sin(p[1]) * hole   -- hole: same angles
    bot_i[i], top_i[i] = m:vert(hx, y, hz), m:vert(hx, y + THICK, hz)
  end
  for i = 1, n do
    local j = i % n + 1
    m:face(bot_o[i], top_o[i], top_o[j], bot_o[j])     -- outer wall with the teeth
    m:face(bot_i[j], top_i[j], top_i[i], bot_i[i])     -- inner wall of the hole (faces inward)
    m:face(top_o[i], top_i[i], top_i[j], top_o[j])     -- top ring
    m:face(bot_o[j], bot_i[j], bot_i[i], bot_o[i])     -- bottom ring
  end
end

-- Step 3: Meshing rule: at the contact point, a tooth of one gear must sit in a gap of the other
-- f is "how far through a tooth" a gear is at the contact angle; neighbours mirror it.
local TOOTH_CENTRE = 0.34                         -- middle of the tooth in the outline above
local function mesh_angle(a_prev, t_prev, t_next, theta)   -- theta: direction from prev to next
  local f = (theta - a_prev) * t_prev / (2 * PI)
  return theta + PI - (2 * PI / t_next) * (-f + 2 * TOOTH_CENTRE + 0.5)
end
local T1, T2, T3 = 24, 12, 18
local R1, R2, R3 = MODULE * T1 / 2, MODULE * T2 / 2, MODULE * T3 / 2
local a1 = t * TURN
local a2 = mesh_angle(a1, T1, T2, 0)
local a3 = mesh_angle(a2, T2, T3, PI / 3)

-- Step 4: Place the centres exactly one pitch-radius sum apart
local x2 = R1 + R2
local x3, z3 = x2 + (R2 + R3) * math.cos(PI / 3), (R2 + R3) * math.sin(PI / 3)
gear(0, 0, T1, a1, 0)
gear(x2, 0, T2, a2, 0)
gear(x3, z3, T3, a3, 0)

-- Step 5: A base plate, so the gears have something to rest on
local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1, (i & 2 == 0) and y0 or y1, (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end
box(-1.3, -0.12, -1.2, 2.9, -0.02, 1.7)

m:build()
print("t =", t)
