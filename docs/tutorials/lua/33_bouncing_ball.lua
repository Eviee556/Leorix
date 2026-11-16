-- A ball that bounces forever, squashing when it lands.
-- Tick "Update every frame" in the Lua panel, then press Play on the Timeline.

-- Step 1: Turn time into a bounce phase that repeats
-- p runs 0 -> 1 during one bounce, then starts again: the % (remainder) makes it loop.
local BOUNCES_PER_SECOND, TOP, R = 1, 3, 0.6
local p = (time * BOUNCES_PER_SECOND) % 1
local height = 4 * p * (1 - p) * TOP              -- a parabola: 0 at both ends, TOP in the middle

-- Step 2: Squash on the ground, stretch while moving fast
-- speed is 1 at the ground and 0 at the top. Only the last fifth squashes,
-- and the sideways scale is 1 / sqrt(tall scale) so the ball keeps its volume.
local speed = math.abs(1 - 2 * p)
local tall = 1 + 0.15 * speed
if speed > 0.8 then tall = 1 - 0.35 * (speed - 0.8) / 0.2 end
local wide = 1 / math.sqrt(tall)
print(string.format("frame %d: height %.2f, tall %.2f", frame, height, tall))

-- Step 3: Build the ball ring by ring, resting on the ground
-- Ring j goes from the top pole (j = 0) to the bottom pole (j = RINGS).
local m = mesh()
local SEG, RINGS = 32, 16
local cy = height + R * tall                       -- the bottom of the ball touches y = height
local top = m:vert(0, cy + R * tall, 0)
local rows = {}
for j = 1, RINGS - 1 do
  local th = PI * j / RINGS
  rows[j] = {}
  for i = 0, SEG - 1 do
    local ph = 2 * PI * i / SEG
    rows[j][i] = m:vert(R * wide * math.sin(th) * math.cos(ph), cy + R * tall * math.cos(th),
                        R * wide * math.sin(th) * math.sin(ph))
  end
end
local bottom = m:vert(0, cy - R * tall, 0)
for i = 0, SEG - 1 do
  local n = (i + 1) % SEG
  m:face(top, rows[1][n], rows[1][i])
  for j = 1, RINGS - 2 do m:face(rows[j][i], rows[j][n], rows[j + 1][n], rows[j + 1][i]) end
  m:face(bottom, rows[RINGS - 1][i], rows[RINGS - 1][n])
end
m:build()

-- Step 4: A floor slab to bounce on
local f = mesh()
local S, D = 2.5, 0.2
local c = {}
for _, q in ipairs({{-S, -D, -S}, {S, -D, -S}, {S, 0, -S}, {-S, 0, -S},
                    {-S, -D,  S}, {S, -D,  S}, {S, 0,  S}, {-S, 0,  S}}) do c[#c + 1] = f:vert(q[1], q[2], q[3]) end
f:face(c[1], c[4], c[3], c[2]); f:face(c[5], c[6], c[7], c[8])
f:face(c[1], c[2], c[6], c[5]); f:face(c[4], c[8], c[7], c[3])
f:face(c[1], c[5], c[8], c[4]); f:face(c[2], c[3], c[7], c[6])
f:build()
