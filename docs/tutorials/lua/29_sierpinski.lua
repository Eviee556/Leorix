-- The Sierpinski pyramid: four half-size copies of itself, again and again.

local m = mesh()
local LEVEL, SIZE = 5, 3.0

-- Step 1: One tetrahedron from four corner points
local function tetra(a, b, c, d)
  local v = {m:vert(a[1], a[2], a[3]), m:vert(b[1], b[2], b[3]), m:vert(c[1], c[2], c[3]), m:vert(d[1], d[2], d[3])}
  -- seen from above, a -> b -> c runs clockwise, so listing them in that order makes the base face down
  m:face(v[1], v[2], v[3])             -- base, facing down
  m:face(v[1], v[4], v[2])
  m:face(v[2], v[4], v[3])
  m:face(v[3], v[4], v[1])
end

-- Step 2: Midpoint of two points
local function mid(p, q) return {(p[1] + q[1]) / 2, (p[2] + q[2]) / 2, (p[3] + q[3]) / 2} end

-- Step 3: Recursion: replace a tetrahedron by four smaller ones in its corners
local count = 0
local function sierpinski(a, b, c, d, level)
  if level == 0 then tetra(a, b, c, d); count = count + 1; return end
  local ab, ac, ad, bc, bd, cd = mid(a, b), mid(a, c), mid(a, d), mid(b, c), mid(b, d), mid(c, d)
  sierpinski(a, ab, ac, ad, level - 1)
  sierpinski(ab, b, bc, bd, level - 1)
  sierpinski(ac, bc, c, cd, level - 1)
  sierpinski(ad, bd, cd, d, level - 1)
end

-- Step 4: Start from a regular tetrahedron standing on the ground
local r = SIZE / math.sqrt(3)
local h = SIZE * math.sqrt(2 / 3)
local A = {r, 0, 0}
local B = {r * math.cos(2 * PI / 3), 0, r * math.sin(2 * PI / 3)}
local Cc = {r * math.cos(4 * PI / 3), 0, r * math.sin(4 * PI / 3)}
local D = {0, h, 0}
sierpinski(A, B, Cc, D, LEVEL)

m:build()
print(count, "tetrahedra")
