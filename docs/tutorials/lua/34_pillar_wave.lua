-- A field of pillars with a ripple running through it, looping perfectly.
-- Tick "Update every frame" in the Lua panel, then press Play on the Timeline.

-- Step 1: A loop measured in frames
-- time keeps growing, but a loop needs a phase that comes back to the start.
-- Counting frames with % LOOP gives exactly LOOP different frames, then repeats.
local LOOP = 48                                   -- frames per loop: 2 seconds at 24 fps
local phase = 2 * PI * ((frame - 1) % LOOP) / LOOP
print(string.format("frame %d of a %d-frame loop (%.1f s at %d fps)", (frame - 1) % LOOP + 1, LOOP, LOOP / fps, fps))

local m = mesh()

-- Step 2: A box helper: centre x/z, bottom at y = 0, given width and height
local function pillar(x, z, w, h)
  local v = {}
  for _, q in ipairs({{-1, 0, -1}, {1, 0, -1}, {1, 1, -1}, {-1, 1, -1},
                      {-1, 0,  1}, {1, 0,  1}, {1, 1,  1}, {-1, 1,  1}}) do
    v[#v + 1] = m:vert(x + q[1] * w / 2, q[2] * h, z + q[3] * w / 2)
  end
  m:face(v[1], v[4], v[3], v[2]); m:face(v[5], v[6], v[7], v[8])
  m:face(v[1], v[2], v[6], v[5]); m:face(v[4], v[8], v[7], v[3])
  m:face(v[1], v[5], v[8], v[4]); m:face(v[2], v[3], v[7], v[6])
end

-- Step 3: Height from a ring-shaped wave that moves outward
-- Each pillar's height depends on its distance from the middle minus the phase,
-- so the crest travels outward. One full phase turn = one wavelength travelled.
local N, GAP = 14, 0.6
for gz = 0, N - 1 do
  for gx = 0, N - 1 do
    local x, z = (gx - (N - 1) / 2) * GAP, (gz - (N - 1) / 2) * GAP
    local d = math.sqrt(x * x + z * z)
    local h = 0.6 + 0.45 * (1 + math.sin(d * 1.6 - phase))
    pillar(x, z, GAP * 0.8, h)
  end
end
m:build()
