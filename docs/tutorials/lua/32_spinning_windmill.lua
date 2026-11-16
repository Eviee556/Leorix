-- A windmill whose sails turn by themselves as the timeline plays.
-- Tick "Update every frame" in the Lua panel, then press Play on the Timeline.

-- Step 1: Read the clock
-- Leorix gives every script three numbers: frame (starts at 1), fps, and
-- time = the seconds since frame 1. Rotation speed is easiest in degrees per second.
local SPEED = 45                                  -- degrees per second
local angle = math.rad(time * SPEED)
print(string.format("frame %d  time %.2f s  sails at %.1f degrees", frame, time, math.deg(angle)))

local m = mesh()

-- Step 2: One helper for any six-sided block, given its 8 corners
-- Corners: the 4 of the back face (z-) then the 4 of the front face (z+),
-- each counter-clockwise, so every face points outward.
local function block(c)
  local v = {}
  for k = 1, 8 do v[k] = m:vert(c[k][1], c[k][2], c[k][3]) end
  m:face(v[1], v[4], v[3], v[2]); m:face(v[5], v[6], v[7], v[8])
  m:face(v[1], v[2], v[6], v[5]); m:face(v[4], v[8], v[7], v[3])
  m:face(v[1], v[5], v[8], v[4]); m:face(v[2], v[3], v[7], v[6])
end

-- Step 3: A tapered tower and a pointed roof
local H, B, T = 6, 1.1, 0.7                        -- height, half width at the base and at the top
block({{-B, 0, -B}, {B, 0, -B}, {T, H, -T}, {-T, H, -T},
       {-B, 0,  B}, {B, 0,  B}, {T, H,  T}, {-T, H,  T}})
local a, b = m:vert(-T, H, -T), m:vert(T, H, -T)
local c, d = m:vert(T, H, T), m:vert(-T, H, T)
local tip = m:vert(0, H + 1.6, 0)
m:face(a, b, c, d)
m:face(a, tip, b); m:face(b, tip, c); m:face(c, tip, d); m:face(d, tip, a)

-- Step 4: Four sails that turn around the hub
-- Each sail is a thin board. Turning its corners by angle + a quarter turn * k
-- around the hub point spins the whole set together.
local HX, HY, HZ = 0, H - 0.6, T + 0.35            -- hub: high on the front of the tower
local function sail(rot)
  local co, si = math.cos(rot), math.sin(rot)
  local function p(x, y, z) return {HX + x * co - y * si, HY + x * si + y * co, HZ + z} end
  local L0, L1, W, D = 0.3, 3.2, 0.35, 0.04         -- from, to, half width, half thickness
  block({p(L0, -W, -D), p(L1, -W, -D), p(L1, W, -D), p(L0, W, -D),
         p(L0, -W,  D), p(L1, -W,  D), p(L1, W,  D), p(L0, W,  D)})
end
for k = 0, 3 do sail(angle + k * PI / 2) end
block({{-0.25, HY - 0.25, T}, {0.25, HY - 0.25, T}, {0.25, HY + 0.25, T}, {-0.25, HY + 0.25, T},
       {-0.25, HY - 0.25, HZ + 0.1}, {0.25, HY - 0.25, HZ + 0.1}, {0.25, HY + 0.25, HZ + 0.1}, {-0.25, HY + 0.25, HZ + 0.1}})
m:build()
