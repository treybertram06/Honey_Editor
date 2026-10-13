# Deferred Renderer Lighting Improvements

Ordered easiest → most involved.

ACES tonemapping, the roughness-aware Fresnel ambient term, and SSAO (all originally
items 1-3 here) are done — see `Renderer3D_DeferredLighting.glsl` (`aces_tonemap`,
`fresnel_schlick_roughness`/`kS`/`kD`) and `renderer_3d_ssao.cpp/.h`.

---

## 1. Bloom
**Effort:** 1–2 days — new post-process passes, no G-buffer changes.

The Bistro's lamp posts and string lights need light bleed. Currently bright emissive surfaces
render as flat discs with hard edges.

### Passes required
1. **Threshold/extract pass** — extract fragments above a luminance threshold (or use the
   emissive G-buffer channel directly as the bloom source for more control).
2. **Downsample chain** — iteratively halve resolution 5–6 times, blending on each step
   (Kawase or dual-filter — avoids the box-blur ringing of older pipelines).
3. **Upsample chain** — progressively upscale back to full resolution, additively accumulating.
4. **Composite** — add bloom texture to HDR color before tonemapping:
   ```glsl
   color += bloom_strength * texture(u_Bloom, v_uv).rgb;
   // then tonemap
   color = aces_tonemap(color);
   ```
   Compositing before tonemapping is important — it ensures bloom participates in HDR compression.

### Notes
- The dual Kawase filter (Next Generation Post Processing in Games, Siggraph 2014) is cheap
  and produces high-quality results. Worth using over a simple gaussian.
- Expose bloom threshold, strength, and scatter as per-scene parameters.
- Emissive-only bloom (sourcing from the G-buffer emissive channel rather than a luminance
  threshold) gives artists precise control over which surfaces bloom and which don't.

---

## 2. IBL — Image-Based Lighting (Skybox GI Approximation)
**Effort:** 3–5 days — offline precomputation shaders, CPU pipeline changes, new bindings.

This is the correct fix for the "dark shadows" problem. Replaces the flat ambient constant with
actual sky-derived indirect lighting using the split-sum approximation (Epic, SIGGRAPH 2013).
Every surface — even fully shadowed — receives a physically plausible contribution from the
environment. Metals reflect the sky, rough surfaces see a blurred version of it, smooth surfaces
see a sharp reflection.

### Three precomputed assets (generated at load time or offline)

**Irradiance map** (`samplerCube`, 32×32 per face)
The diffuse irradiance integral of the environment — how much diffuse light arrives from each
direction. Computed by convolving the skybox cubemap over the hemisphere for each output texel.
One-time compute shader pass per environment.

**Pre-filtered environment map** (`samplerCube`, mip pyramid — 256×256 to 1×1)
Stores the specular radiance integral at varying roughness levels. Mip 0 = mirror-sharp,
highest mip = fully blurred. Computed via importance-sampled GGX convolution per mip level.

**BRDF integration LUT** (`sampler2D`, 512×512 R16G16)
A 2D lookup table indexed by (NdotV, roughness) that stores the scale and bias of the specular
BRDF integral. Only needs to be computed once ever (roughness/NdotV are universal, not
scene-dependent). Can be baked to disk and shipped as an asset.

### Shader changes (in `Renderer3D_DeferredLighting.glsl`)

Add three new bindings (set=1, bindings 6–8):
```glsl
layout(set = 1, binding = 6) uniform samplerCube u_IrradianceMap;
layout(set = 1, binding = 7) uniform samplerCube u_PrefilteredEnvMap;
layout(set = 1, binding = 8) uniform sampler2D   u_BrdfLut;
```

Replace the flat ambient in `main()`:
```glsl
// Diffuse indirect
vec3 irradiance      = texture(u_IrradianceMap, N).rgb;
vec3 diffuse_indirect = irradiance * albedo * kD * ao;

// Specular indirect (split-sum)
const float MAX_REFLECTION_LOD = 8.0;
vec3 R           = reflect(-V, N);
vec3 prefiltered = textureLod(u_PrefilteredEnvMap, R, roughness * MAX_REFLECTION_LOD).rgb;
vec2 brdf        = texture(u_BrdfLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
vec3 spec_indirect = prefiltered * (kS * brdf.x + brdf.y) * ao;

vec3 ambient = (diffuse_indirect + spec_indirect) * ibl_intensity;
```

### C++ side
- Add `IBLPrecompute` system (or integrate into `ShaderCache` / `VkBackend`) that runs
  the three precomputation passes on startup when a sky environment is set.
- Store the three precomputed textures on the scene or renderer, update descriptors when
  the environment changes.
- Expose `ibl_intensity` as a per-scene float (default 1.0) so the indirect strength can
  be balanced against direct light intensity.

### Optional extension: Reflection Capture Probes
For indoor areas (inside the restaurant, under awnings) where the skybox is not visible,
local reflection probes capture a cubemap at a world position and blend it in based on
distance/influence volume. This is how Unreal handles mixed indoor/outdoor indirect lighting.
Start with a single global sky IBL first, add probes later if needed.

---

## Further Recommendations

### Fog / Atmospheric Scattering
The Bistro's background buildings have zero aerial perspective, which flattens the sense of
depth. Even a simple exponential depth fog added before tonemapping adds significant realism.
A proper analytical atmosphere model (Preetham or Hillaire) would also drive the sky color
and directional light color automatically from sun elevation, eliminating manual tweaking.

### Temporal Anti-Aliasing (TAA)
The current output is almost certainly aliased on geometry edges and specular highlights.
TAA is a post-process pass that accumulates jittered samples across frames and blends them
with a history buffer. Dramatically improves perceived quality with a modest GPU cost.
Required if SSAO is added (SSAO noise becomes invisible with TAA).

### Screen-Space Reflections (SSR)
For flat/wet surfaces (the cobblestones after rain, restaurant windows), SSR ray-marches
the depth buffer in screen space to find reflection hits. Cheaper than probes for surfaces
that are mostly horizontal. Falls back to IBL when the ray leaves the screen.

### Contact Shadows
A separate screen-space ray-march from the fragment toward the light to catch small-scale
self-shadowing missed by the shadow map (hair, cloth, small props). Very cheap (8–16 steps),
high perceptual quality gain on close geometry.

### Volumetric Lighting / God Rays
Ray-march through the scene from fragment to the directional light, sampling the shadow map
at each step. Produces light shafts through the Bistro's narrow street geometry. Medium
cost, high atmospheric impact.

### Exposure / Auto-Exposure
A compute pass that reads the average log-luminance of the frame into a 1×1 texture via
histogram downsampling, then smoothly adjusts the exposure multiplier toward a target EV.
Prevents the scene from appearing over/underexposed as the camera moves.