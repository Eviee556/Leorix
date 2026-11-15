-- Menger sponge: recursive generation.
-- Each cube splits into 27 sub-cubes; the 7 in the centre of a face or the core are removed.

local LEVEL = 2          -- 2 -> 400 cubes; 3 -> 8000 cubes (slow-ish, still fine)
local SIZE  = 2.4

local m = mesh()

local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1,
                  (i & 2 == 0) and y0 or y1,
                  (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end

local count = 0
local function sponge(x, y, z, s, level)
  if level == 0 then
    box(x, y, z, x + s, y + s, z + s)
    count = count + 1
    return
  end
  local t = s / 3
  for i = 0, 2 do
    for j = 0, 2 do
      for k = 0, 2 do
        local centres = (i == 1 and 1 or 0) + (j == 1 and 1 or 0) + (k == 1 and 1 or 0)
        if centres < 2 then                       -- keep edges and corners only
          sponge(x + i * t, y + j * t, z + k * t, t, level - 1)
        end
      end
    end
  end
end

sponge(-SIZE / 2, 0, -SIZE / 2, SIZE, LEVEL)
m:build()
print(count, "cubes")
