-- An L-system tree: grow a string of drawing commands, then let a 3D "turtle" draw it.
--   F  move forward, drawing a branch      X  a growing tip (a leaf cluster once growth stops)
--   +- turn left/right   &^ pitch down/up   /\ roll   [ ] save / restore the turtle

local m = mesh()
local ITERATIONS, ANGLE, ROLL, SEED = 6, 32, 120, 3

-- Step 1: Rewrite the string a few times
local rules = { X = "F[&X]/[&X]/[&X]", F = "F" }   -- every tip splits into three
local s = "FFX"
for _ = 1, ITERATIONS do
  local out = {}
  for ch in s:gmatch(".") do out[#out + 1] = rules[ch] or ch end
  s = table.concat(out)
end
print(#s, "commands")

-- Step 2: A turtle needs a position and three directions
local function rot(a, b, deg)                 -- turn the pair (a, b) by deg degrees
  local r = math.rad(deg)
  local c, sn = math.cos(r), math.sin(r)
  return {a[1] * c + b[1] * sn, a[2] * c + b[2] * sn, a[3] * c + b[3] * sn},
         {b[1] * c - a[1] * sn, b[2] * c - a[2] * sn, b[3] * c - a[3] * sn}
end
local turtle = {pos = {0, 0, 0}, H = {0, 1, 0}, L = {-1, 0, 0}, U = {0, 0, 1}, depth = 0}

-- Step 3: A tapered branch between two points, oriented by the turtle
local SIDES = 6
local function branch(p, q, r0, r1, L, U)
  local a, b = {}, {}
  for i = 1, SIDES do
    local t = (i - 1) / SIDES * 2 * PI
    local c, sn = math.cos(t), math.sin(t)
    local d = {L[1] * c + U[1] * sn, L[2] * c + U[2] * sn, L[3] * c + U[3] * sn}
    a[i] = m:vert(p[1] + d[1] * r0, p[2] + d[2] * r0, p[3] + d[3] * r0)
    b[i] = m:vert(q[1] + d[1] * r1, q[2] + d[2] * r1, q[3] + d[3] * r1)
  end
  for i = 1, SIDES do
    local j = i % SIDES + 1
    m:face(a[i], a[j], b[j], b[i])
  end
  -- close both ends so every branch is a solid with a clear inside and outside
  -- the ring turns counter-clockwise around the heading, so the far end lists it forwards
  -- and the near end backwards
  local near = {}
  for i = SIDES, 1, -1 do near[#near + 1] = a[i] end
  m:face(table.unpack(near)); m:face(table.unpack(b))
end

-- Step 4: A small leaf cluster: an octahedron
local function leaf(p, size)
  local v = {
    m:vert(p[1] + size, p[2], p[3]), m:vert(p[1] - size, p[2], p[3]),
    m:vert(p[1], p[2] + size, p[3]), m:vert(p[1], p[2] - size * 0.6, p[3]),
    m:vert(p[1], p[2], p[3] + size), m:vert(p[1], p[2], p[3] - size)}
  local F = {{1, 3, 5}, {5, 3, 2}, {2, 3, 6}, {6, 3, 1}, {5, 4, 1}, {2, 4, 5}, {6, 4, 2}, {1, 4, 6}}
  for _, f in ipairs(F) do m:face(v[f[1]], v[f[2]], v[f[3]]) end
end

-- Step 5: Read the string and move the turtle
local function jitter(i) return (((i * 2654435761 + SEED * 97) & 0xffff) / 65535 - 0.5) end
local stack = {}
for i = 1, #s do
  local c = s:sub(i, i)
  local d = turtle.depth
  if c == "F" then
    local len = 0.8 * 0.78 ^ d
    local q = {turtle.pos[1] + turtle.H[1] * len, turtle.pos[2] + turtle.H[2] * len, turtle.pos[3] + turtle.H[3] * len}
    local r = 0.12 * 0.68 ^ d
    branch(turtle.pos, q, r, r * 0.85, turtle.L, turtle.U)
    turtle.pos = q
  elseif c == "X" then
    leaf(turtle.pos, 0.16)                      -- every tip left over becomes a leaf cluster
  elseif c == "&" then turtle.H, turtle.U = rot(turtle.H, turtle.U, ANGLE + jitter(i) * 14)
  elseif c == "/" then turtle.L, turtle.U = rot(turtle.L, turtle.U, ROLL + jitter(i) * 30)
  elseif c == "[" then
    stack[#stack + 1] = {pos = turtle.pos, H = turtle.H, L = turtle.L, U = turtle.U, depth = turtle.depth}
    turtle.depth = turtle.depth + 1
  elseif c == "]" then
    turtle = table.remove(stack)
  end
end

m:build()
