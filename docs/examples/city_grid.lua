-- Instanced grid: a block of "buildings" whose heights come from a wave function.
-- Built-in primitives always sit at the origin, so instancing means emitting
-- your own boxes into one mesh at different positions.

local N      = 14         -- buildings per side
local PITCH  = 0.5        -- distance between building centres
local WIDTH  = 0.38       -- building footprint

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

local half = (N - 1) * PITCH / 2
for gx = 0, N - 1 do
  for gz = 0, N - 1 do
    local x, z = gx * PITCH - half, gz * PITCH - half
    local rnd = (math.sin(gx * 12.9898 + gz * 78.233) * 43758.5453) % 1   -- cheap per-cell random
    local h = 0.15 + 3.2 * math.exp(-(x * x + z * z) * 0.12) * (0.35 + 0.65 * rnd)
    local w = WIDTH / 2
    box(x - w, 0, z - w, x + w, h, z + w)
  end
end

-- a thin base slab under the whole block
box(-half - 0.4, -0.08, -half - 0.4, half + 0.4, 0, half + 0.4)

m:build()
print(N * N, "buildings")
