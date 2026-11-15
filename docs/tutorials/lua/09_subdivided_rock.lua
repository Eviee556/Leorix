-- Subdividing by hand: from an 8-sided octahedron to a lumpy boulder.

local m = mesh()
local LEVELS, SEED = 4, 11

-- Step 1: Start from an octahedron, stored as plain Lua tables
local P = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}
local F = {{1, 3, 5}, {5, 3, 2}, {2, 3, 6}, {6, 3, 1}, {5, 4, 1}, {2, 4, 5}, {6, 4, 2}, {1, 4, 6}}

-- Step 2: Split every triangle into four, sharing edge midpoints
local function subdivide(points, faces)
  local cache, out = {}, {}
  local function mid(a, b)
    local key = a < b and (a .. "_" .. b) or (b .. "_" .. a)   -- same edge, same key
    if cache[key] then return cache[key] end
    local p, q = points[a], points[b]
    local x, y, z = (p[1] + q[1]) / 2, (p[2] + q[2]) / 2, (p[3] + q[3]) / 2
    local l = math.sqrt(x * x + y * y + z * z)
    points[#points + 1] = {x / l, y / l, z / l}                   -- push onto the sphere
    cache[key] = #points
    return #points
  end
  for _, f in ipairs(faces) do
    local a, b, c = f[1], f[2], f[3]
    local ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
    out[#out + 1] = {a, ab, ca}; out[#out + 1] = {ab, b, bc}
    out[#out + 1] = {ca, bc, c}; out[#out + 1] = {ab, bc, ca}
  end
  return out
end
for _ = 1, LEVELS do F = subdivide(P, F) end

-- Step 3: A tiny 3D value noise to make lumps
local function hash(x, y, z)
  local h = (x * 374761393 + y * 668265263 + z * 2147483647 + SEED * 1274126177) & 0xffffffff
  h = ((h ~ (h >> 13)) * 1274126177) & 0xffffffff
  return ((h ~ (h >> 16)) & 0xffff) / 65535
end
local function noise(x, y, z)
  local xi, yi, zi = math.floor(x), math.floor(y), math.floor(z)
  local fx, fy, fz = x - xi, y - yi, z - zi
  fx, fy, fz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
  local function L(a, b, t) return a + (b - a) * t end
  local function H(dx, dy, dz) return hash(xi + dx, yi + dy, zi + dz) end
  return L(L(L(H(0,0,0), H(1,0,0), fx), L(H(0,1,0), H(1,1,0), fx), fy),
           L(L(H(0,0,1), H(1,0,1), fx), L(H(0,1,1), H(1,1,1), fx), fy), fz)
end

-- Step 4: Push every point in or out, squash it, flatten the bottom
for i, p in ipairs(P) do
  local n = noise(p[1] * 2 + 10, p[2] * 2 + 10, p[3] * 2 + 10) * 0.6
          + noise(p[1] * 5 + 20, p[2] * 5 + 20, p[3] * 5 + 20) * 0.3
  local r = 0.6 + n * 0.9
  local y = p[2] * r * 0.85
  P[i] = {p[1] * r * 1.2, math.max(y, -0.25), p[3] * r}
end

-- Step 5: Hand the result to Leorix
local ids = {}
for i, p in ipairs(P) do ids[i] = m:vert(p[1], p[2], p[3]) end
for _, f in ipairs(F) do m:face(ids[f[1]], ids[f[2]], ids[f[3]]) end
m:build()
print(#P, "points,", #F, "triangles")
