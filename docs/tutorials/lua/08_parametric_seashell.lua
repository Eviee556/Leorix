-- A parametric seashell: a surface defined by a formula P(u, v).
-- u winds along the spiral, v goes around the tube.

local m = mesh()
local U, V = 220, 40                   -- samples along the spiral and around the tube
local TURNS = 4.5

-- Step 1: Write the surface as a function of two numbers
-- A logarithmic spiral: the tube grows by the same factor every turn.
local function shell(u, v)
  local a = u * TURNS * 2 * PI                 -- angle along the spiral
  local grow = math.exp(0.11 * a)              -- the tube doubles in size every turn
  local r = 0.05 * grow                        -- tube radius
  local R = 0.085 * grow                       -- distance of the tube from the axis
  local cv, sv = math.cos(v * 2 * PI), math.sin(v * 2 * PI)
  local dist = R + r * cv
  local x = math.cos(a) * dist
  local z = math.sin(a) * dist
  local y = -0.15 * grow + r * sv              -- each turn sits lower: the shell's cone
  return x, y, z
end

-- Step 2: Sample it on a grid and keep the vertex indices
local idx = {}
for j = 0, U do
  idx[j] = {}
  for i = 0, V - 1 do
    idx[j][i] = m:vert(shell(j / U, i / V))
  end
end

-- Step 3: Join the grid with quads; v wraps around, u doesn't
for j = 0, U - 1 do
  for i = 0, V - 1 do
    local k = (i + 1) % V
    m:face(idx[j][i], idx[j][k], idx[j + 1][k], idx[j + 1][i])
  end
end

-- Step 4: Close the open mouth with a single n-gon
local mouth = {}
for i = 0, V - 1 do mouth[#mouth + 1] = idx[U][i] end
m:face(table.unpack(mouth))

m:build()
