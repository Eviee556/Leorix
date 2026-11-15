-- A small house built face by face: triangles, quads, n-gons and winding.

local m = mesh()
local W, D, H, RIDGE = 2.0, 3.0, 1.6, 1.0     -- width (x), depth (z), wall height, roof rise

-- Step 1: Place the corner points
-- Front wall at z = +D/2, back wall at z = -D/2. Five points each: a pentagon.
local function gable(z)
  return {
    m:vert(-W / 2, 0, z),            -- 1 bottom left
    m:vert( W / 2, 0, z),            -- 2 bottom right
    m:vert( W / 2, H, z),            -- 3 eave right
    m:vert( 0, H + RIDGE, z),        -- 4 ridge
    m:vert(-W / 2, H, z),            -- 5 eave left
  }
end
local f, b = gable(D / 2), gable(-D / 2)

-- Step 2: One n-gon per gable wall
-- Seen from the front, f[1..5] goes counter-clockwise, so the normal faces +Z (outward).
m:face(f[1], f[2], f[3], f[4], f[5])
-- The back wall must face -Z, so we list the same corners in the opposite order.
m:face(b[5], b[4], b[3], b[2], b[1])

-- Step 3: Quads for the side walls, the roof and the floor
m:face(f[2], b[2], b[3], f[3])     -- right wall  (+X)
m:face(b[1], f[1], f[5], b[5])     -- left wall   (-X)
m:face(f[3], b[3], b[4], f[4])     -- right roof slope
m:face(b[5], f[5], f[4], b[4])     -- left roof slope
m:face(b[1], b[2], f[2], f[1])     -- floor       (-Y)

-- Step 4: A chimney: a box made of six quads
local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1, (i & 2 == 0) and y0 or y1, (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end
box(0.35, H + 0.2, -0.9, 0.7, H + RIDGE + 0.35, -0.5)

-- Step 5: A door step and a little triangle sign over the door
box(-0.4, 0, D / 2, 0.4, 0.12, D / 2 + 0.35)
local s1 = m:vert(-0.3, 1.15, D / 2 + 0.01)
local s2 = m:vert( 0.3, 1.15, D / 2 + 0.01)
local s3 = m:vert( 0.0, 1.45, D / 2 + 0.01)
m:face(s1, s2, s3)                 -- a lone triangle, counter-clockwise from the front

m:build()
