-- A seeded maze: the "recursive backtracker" algorithm, then walls as boxes.

local m = mesh()
local W, H, SEED = 14, 14, 9           -- cells across and down
local CELL, WALL, WALL_H = 0.5, 0.08, 0.35

-- Step 1: A tiny random number generator with a seed (same seed, same maze)
local state = SEED
local function rand(n)                 -- whole number from 1 to n
  state = (state * 1103515245 + 12345) & 0x7fffffff
  return state % n + 1
end

-- Step 2: Every cell starts with all four walls; we remember which cells we have visited
local visited, open = {}, {}           -- open["x,y,dir"] = true once a wall is knocked down
local function key(x, y) return y * W + x end
local DIRS = {{1, 0, "E", "W"}, {-1, 0, "W", "E"}, {0, 1, "S", "N"}, {0, -1, "N", "S"}}

-- Step 3: Walk: step to a random unvisited neighbour, knocking the wall down; backtrack at dead ends
local stack = {{0, 0}}
visited[key(0, 0)] = true
while #stack > 0 do
  local cur = stack[#stack]
  local options = {}
  for _, d in ipairs(DIRS) do
    local nx, ny = cur[1] + d[1], cur[2] + d[2]
    if nx >= 0 and ny >= 0 and nx < W and ny < H and not visited[key(nx, ny)] then
      options[#options + 1] = {nx, ny, d}
    end
  end
  if #options == 0 then
    table.remove(stack)                -- dead end: go back
  else
    local pick = options[rand(#options)]
    local d = pick[3]
    open[cur[1] .. "," .. cur[2] .. "," .. d[3]] = true
    open[pick[1] .. "," .. pick[2] .. "," .. d[4]] = true
    visited[key(pick[1], pick[2])] = true
    stack[#stack + 1] = {pick[1], pick[2]}
  end
end

-- Step 4: Turn the remaining walls into boxes
local function box(x0, y0, z0, x1, y1, z1)
  local v = {}
  for i = 0, 7 do
    v[i] = m:vert((i & 1 == 0) and x0 or x1, (i & 2 == 0) and y0 or y1, (i & 4 == 0) and z0 or z1)
  end
  m:face(v[0], v[4], v[6], v[2]); m:face(v[1], v[3], v[7], v[5])
  m:face(v[0], v[1], v[5], v[4]); m:face(v[2], v[6], v[7], v[3])
  m:face(v[0], v[2], v[3], v[1]); m:face(v[4], v[5], v[7], v[6])
end
local ox, oz = -W * CELL / 2, -H * CELL / 2
local h = WALL / 2
open["0,0,N"] = true                   -- entrance
open[(W - 1) .. "," .. (H - 1) .. ",S"] = true   -- exit
local walls = 0
for y = 0, H - 1 do
  for x = 0, W - 1 do
    local x0, z0 = ox + x * CELL, oz + y * CELL
    if not open[x .. "," .. y .. ",N"] then box(x0 - h, 0, z0 - h, x0 + CELL + h, WALL_H, z0 + h); walls = walls + 1 end
    if not open[x .. "," .. y .. ",W"] then box(x0 - h, 0, z0 - h, x0 + h, WALL_H, z0 + CELL + h); walls = walls + 1 end
    if y == H - 1 and not open[x .. "," .. y .. ",S"] then box(x0 - h, 0, z0 + CELL - h, x0 + CELL + h, WALL_H, z0 + CELL + h); walls = walls + 1 end
    if x == W - 1 then box(x0 + CELL - h, 0, z0 - h, x0 + CELL + h, WALL_H, z0 + CELL + h); walls = walls + 1 end
  end
end

-- Step 5: A floor slab under the maze
box(ox - 0.3, -0.06, oz - 0.3, -ox + 0.3, 0, -oz + 0.3)

m:build()
print(walls, "wall pieces")
