# Showcase: Lantern Isle

![Lantern Isle](../images/hero.png)

A floating island at night: a cottage with lit windows, lantern posts strung with a sagging cable,
a glass orb with a glowing seed on a stone plinth, trees, crates and a barrel, flowers and fireflies,
and a stream that pours off the edge as a waterfall into a sea of moonlit clouds.

Everything in the picture comes from one Lua scene script, [`lantern_isle.lua`](lantern_isle.lua),
and was rendered by Leorix Native's own CPU path tracer. Nothing was modelled by hand, painted over
or edited afterwards.

## How it was made

- **Terrain.** Value noise (fBm) written in the script shapes the grassy top, the rim and the layered
  rock underside, which tapers to a jagged point with strata bands.
- **Buildings and props.** Boxes, frusta and noise-displaced blobs built with `mesh()`, `m:vert()` and
  `m:face()`, placed in the cottage's own rotated frame.
- **Curves.** The lantern cable and the stream are swept tubes made with `curve()`, Leorix's curve code
  with rotation-minimising frames, so the tube never twists.
- **Materials.** `material{}` sets plaster, timber, roof tiles, metal posts, glass (`glass=1.5`) and
  the emissive windows, lanterns, orb seed and fireflies. The emissive surfaces light the scene through
  next-event estimation with multiple importance sampling.
- **Light and camera.** `world{}` picks the built-in Night environment with stars and adds cool
  moonlight as the sun. `camera{}` frames the shot with a 36 mm lens and a little depth of field.

## Re-render it

```sh
leorix.exe --headless-render docs/showcase/lantern_isle.lua hero.png --width 1920 --height 1080 --spp 640
```

This runs without opening a window or using the GPU. On a 16-thread desktop CPU it takes a few minutes.
Add `--save lantern_isle.leorix` to also write the scene as a project. A copy is already here as
[`lantern_isle.leorix`](lantern_isle.leorix), so you can open it in the app and look around. The
project stores the objects, materials and camera; set the environment to the Night preset to match
the lighting.
