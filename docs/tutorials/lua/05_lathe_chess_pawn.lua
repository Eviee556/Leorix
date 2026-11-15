-- A chess pawn made on a lathe: spin a 2D profile around the Y axis.

local m = mesh()
local SEG = 48                         -- slices around the axis

-- Step 1: Draw the side profile as {radius, height} pairs, bottom to top
local profile = {
  {0.62, 0.00}, {0.62, 0.10}, {0.55, 0.16}, {0.56, 0.22},   -- base
  {0.44, 0.30}, {0.34, 0.40}, {0.27, 0.60}, {0.22, 0.85},   -- body
  {0.20, 1.02}, {0.36, 1.08}, {0.36, 1.13}, {0.20, 1.18},   -- collar
}
-- Step 2: Finish with a round head: points on a circle, ending at radius 0
local HEAD_R, HEAD_Y = 0.3, 1.42
for k = 1, 10 do
  local a = -PI / 2 + 0.35 + k / 10 * (PI - 0.35)          -- from below the equator up to the pole
  local r = math.cos(a) * HEAD_R
  if k == 10 then r = 0 end
  profile[#profile + 1] = {r, HEAD_Y + math.sin(a) * HEAD_R}
end

-- Step 3: Spin every profile point into a ring
local rings = {}
for j, p in ipairs(profile) do
  local r, y = p[1], p[2]
  if r == 0 then
    rings[j] = {pole = m:vert(0, y, 0)}                     -- a single point at the tip
  else
    local ring = {}
    for i = 1, SEG do
      local a = (i - 1) / SEG * 2 * PI
      ring[i] = m:vert(math.cos(a) * r, y, math.sin(a) * r)
    end
    rings[j] = ring
  end
end

-- Step 4: Stitch neighbouring rings with quads, and the tip with triangles
for j = 1, #rings - 1 do
  local lo, hi = rings[j], rings[j + 1]
  for i = 1, SEG do
    local k = i % SEG + 1
    if hi.pole then
      m:face(lo[i], hi.pole, lo[k])
    else
      m:face(lo[i], hi[i], hi[k], lo[k])
    end
  end
end

-- Step 5: Close the bottom with one n-gon
m:face(table.unpack(rings[1]))

m:build()
