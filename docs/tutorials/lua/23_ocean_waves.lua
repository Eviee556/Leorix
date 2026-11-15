-- Ocean waves that loop: a sum of Gerstner waves, sampled at time t.

-- Step 1: Time, plus the rule that makes the loop seamless
-- Every wave moves a whole number of cycles while t goes 0 -> 1, so frame 1 equals frame 0.
local t = T or 0.0
local RES, SIZE, BASE = 110, 8.0, -0.6

local m = mesh()

-- Step 2: Describe each wave: direction, wavelength, height, steepness, cycles per loop
local waves = {
  {dir = {1.0, 0.2},  len = 2.6, amp = 0.16, steep = 0.7, cycles = 1},
  {dir = {0.6, 0.8},  len = 1.5, amp = 0.08, steep = 0.6, cycles = 2},
  {dir = {-0.3, 1.0}, len = 0.9, amp = 0.04, steep = 0.5, cycles = 3},
  {dir = {0.9, -0.5}, len = 0.6, amp = 0.02, steep = 0.4, cycles = 4},
}
for _, w in ipairs(waves) do
  local l = math.sqrt(w.dir[1] ^ 2 + w.dir[2] ^ 2)
  w.dir = {w.dir[1] / l, w.dir[2] / l}
  w.k = 2 * PI / w.len
end

-- Step 3: A Gerstner wave moves points in circles: sideways as well as up
local function ocean(x, z)
  local px, py, pz = x, 0, z
  for _, w in ipairs(waves) do
    local phase = w.k * (w.dir[1] * x + w.dir[2] * z) - w.cycles * 2 * PI * t
    local q = w.steep / (w.k * #waves)          -- keeps crests from folding over
    px = px + q * w.dir[1] * math.cos(phase) * w.amp * w.k
    pz = pz + q * w.dir[2] * math.cos(phase) * w.amp * w.k
    py = py + w.amp * math.sin(phase)
  end
  return px, py, pz
end

-- Step 4: Sample the grid, then close it into a block like a terrain diorama
local idx, border, bottom = {}, {}, {}
for j = 0, RES do
  idx[j] = {}
  for i = 0, RES do
    local x, z = (i / RES - 0.5) * SIZE, (j / RES - 0.5) * SIZE
    local px, py, pz = ocean(x, z)
    if i == 0 or j == 0 or i == RES or j == RES then px, pz = x, z end  -- keep the edges straight
    idx[j][i] = m:vert(px, py, pz)
  end
end
for j = 0, RES - 1 do
  for i = 0, RES - 1 do m:face(idx[j][i], idx[j + 1][i], idx[j + 1][i + 1], idx[j][i + 1]) end
end
for i = 0, RES do border[#border + 1] = {i, 0} end
for j = 1, RES do border[#border + 1] = {RES, j} end
for i = RES - 1, 0, -1 do border[#border + 1] = {i, RES} end
for j = RES - 1, 1, -1 do border[#border + 1] = {0, j} end
local top = {}
for k, c in ipairs(border) do
  top[k] = idx[c[2]][c[1]]
  bottom[k] = m:vert((c[1] / RES - 0.5) * SIZE, BASE, (c[2] / RES - 0.5) * SIZE)
end
for k = 1, #border do
  local n = k % #border + 1
  m:face(top[k], top[n], bottom[n], bottom[k])
end
m:face(table.unpack(bottom))

m:build()
print("t =", t)
