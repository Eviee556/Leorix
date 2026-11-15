-- Basalt columns: hexagonal prisms whose heights come from cellular (Voronoi-style) noise.

local m = mesh()
local RINGS, R, SEED = 13, 0.16, 6     -- hex rings around the centre, column radius

-- Step 1: Hash for random feature points
local function hash(ix, iz, k)
  local h = (ix * 374761393 + iz * 668265263 + k * 1442695041 + SEED * 2246822519) & 0xffffffff
  h = ((h ~ (h >> 13)) * 1274126177) & 0xffffffff
  return ((h ~ (h >> 16)) & 0xffffff) / 16777216
end

-- Step 2: Cellular noise: one random point per grid square; measure distance to the nearest two
local SCALE = 1.1
local function cellular(x, z)
  local gx, gz = math.floor(x / SCALE), math.floor(z / SCALE)
  local f1, f2, id = 1e9, 1e9, 0
  for dz = -1, 1 do
    for dx = -1, 1 do
      local cx, cz = gx + dx, gz + dz
      local px = (cx + hash(cx, cz, 1)) * SCALE
      local pz = (cz + hash(cx, cz, 2)) * SCALE
      local d = math.sqrt((x - px) ^ 2 + (z - pz) ^ 2)
      if d < f1 then f2, f1, id = f1, d, cx * 7919 + cz elseif d < f2 then f2 = d end
    end
  end
  return f1, f2, id
end

-- Step 3: A hexagonal prism with a flat top
local function hex_column(cx, cz, r, h)
  local lo, hi = {}, {}
  for i = 1, 6 do
    local a = (i - 1) / 6 * 2 * PI + PI / 6
    lo[i] = m:vert(cx + math.cos(a) * r, 0, cz + math.sin(a) * r)
    hi[i] = m:vert(cx + math.cos(a) * r, h, cz + math.sin(a) * r)
  end
  for i = 1, 6 do local j = i % 6 + 1; m:face(lo[i], hi[i], hi[j], lo[j]) end
  m:face(hi[6], hi[5], hi[4], hi[3], hi[2], hi[1])
  m:face(lo[1], lo[2], lo[3], lo[4], lo[5], lo[6])
end

-- Step 4: Lay columns out on a hex grid (axial coordinates q, r)
local count = 0
for q = -RINGS, RINGS do
  for r = math.max(-RINGS, -q - RINGS), math.min(RINGS, -q + RINGS) do
    local x = R * math.sqrt(3) * (q + r / 2)
    local z = R * 1.5 * r
    -- Step 5: Height: cells step up towards their centres, and the whole field rises in the middle
    local f1, f2, id = cellular(x, z)
    local cell_h = 0.35 + 0.6 * ((id * 2654435761) % 1000) / 1000
    local edge = math.min(1, (f2 - f1) * 3)              -- 0 on a cell border, 1 inside
    local mound = 1 - math.min(1, math.sqrt(x * x + z * z) / (RINGS * R * 1.6))
    local h = 0.15 + (cell_h * (0.5 + 0.5 * edge)) * (0.3 + 2.4 * mound * mound)
    hex_column(x, z, R * 0.97, h)
    count = count + 1
  end
end

m:build()
print(count, "columns")
