-- A flower that blooms: petals swing open as t goes from 0 to 1.

-- Step 1: Time, eased so the bloom starts slowly and settles gently
local t = T or 1.0
local ease = t * t * (3 - 2 * t)
local PETALS, LAYERS = 8, 2

local m = mesh()

-- Step 2: A closed ellipsoid, oriented by three axes (so every petal is solid)
local function ellipsoid(c, ax, ay, az, rx, ry, rz, seg, rings)
  local rowsv = {}
  for j = 0, rings do
    local th = j / rings * PI
    local row = {}
    for i = 1, seg do
      local ph = (i - 1) / seg * 2 * PI
      local x, y, z = math.sin(th) * math.cos(ph) * rx, math.cos(th) * ry, math.sin(th) * math.sin(ph) * rz
      row[i] = m:vert(c[1] + ax[1] * x + ay[1] * y + az[1] * z,
                      c[2] + ax[2] * x + ay[2] * y + az[2] * z,
                      c[3] + ax[3] * x + ay[3] * y + az[3] * z)
      if j == 0 or j == rings then break end    -- the poles are single points
    end
    rowsv[j] = row
  end
  for j = 0, rings - 1 do
    for i = 1, seg do
      local k = i % seg + 1
      if j == 0 then m:face(rowsv[0][1], rowsv[1][k], rowsv[1][i])
      elseif j == rings - 1 then m:face(rowsv[j][i], rowsv[j][k], rowsv[rings][1])
      else m:face(rowsv[j][i], rowsv[j][k], rowsv[j + 1][k], rowsv[j + 1][i]) end
    end
  end
end

-- Step 3: The stem and the flower head
local STEM_H = 1.6
local ring_lo, ring_hi = {}, {}
for i = 1, 8 do
  local a = (i - 1) / 8 * 2 * PI
  ring_lo[i] = m:vert(math.cos(a) * 0.045, 0, math.sin(a) * 0.045)
  ring_hi[i] = m:vert(math.cos(a) * 0.035, STEM_H, math.sin(a) * 0.035)
end
for i = 1, 8 do local j = i % 8 + 1; m:face(ring_lo[i], ring_hi[i], ring_hi[j], ring_lo[j]) end
m:face(table.unpack(ring_lo))
local head = {0, STEM_H + 0.05, 0}
ellipsoid(head, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 0.16, 0.1, 0.16, 16, 8)

-- Step 4: Each petal leans out by an angle that grows with t
for layer = 1, LAYERS do
  local closed, open = math.rad(8), math.rad(layer == 1 and 80 or 55)
  local tilt = closed + (open - closed) * ease
  for p = 0, PETALS - 1 do
    local yaw = (p + (layer - 1) * 0.5) / PETALS * 2 * PI
    local out = {math.cos(yaw), 0, math.sin(yaw)}        -- horizontal direction of this petal
    local side = {-math.sin(yaw), 0, math.cos(yaw)}
    -- the petal's long axis: tilt away from straight up towards `out`
    local long = {out[1] * math.sin(tilt), math.cos(tilt), out[3] * math.sin(tilt)}
    -- side, long, thick must form a right-handed set, or the petal is built inside out
    local thick = {-out[1] * math.cos(tilt), math.sin(tilt), -out[3] * math.cos(tilt)}
    local L = layer == 1 and 0.55 or 0.42
    local c = {head[1] + long[1] * L * 0.5, head[2] + long[2] * L * 0.5, head[3] + long[3] * L * 0.5}
    -- Step 5: Place the petal so its base sits on the flower head
    ellipsoid(c, side, long, thick, 0.13, L * 0.5, 0.025, 10, 8)
  end
end

m:build()
print("t =", t)
