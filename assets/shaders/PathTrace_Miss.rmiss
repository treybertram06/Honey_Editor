#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload {
    vec3  radiance;
    float brdf_pdf;
    vec3  throughput;
    float _pad1;
    vec3  next_origin;
    float _pad2;
    vec3  next_direction;
    uint  seed;
    vec3  hit_normal;
    float hit_depth;
};
layout(location = 0) rayPayloadInEXT HitPayload payload;

struct DirectionalLight { vec3 direction; float intensity; vec3 color; int point_light_count; };
struct PointLight        { vec3 position;  float intensity; vec3 color; float range; };
layout(set = 0, binding = 5) uniform LightsUBO {
    DirectionalLight u_directional;
    PointLight       u_point_lights[32];
} u_lights;

// ─── Simple physically-inspired sky ──────────────────────────────────────────
// Based on a power-law sky dome gradient + sun disc with halo glow.
// The sun direction is taken from the directional light (negated — light.direction points
// toward ground, we need the direction toward the sky).

vec3 sky_color(vec3 ray_dir) {
    // Sun direction (toward sky, not toward ground)
    vec3 sun_dir = normalize(-u_lights.u_directional.direction);

    // Sky dome: interpolate between a horizon color and a zenith color.
    // Luminance scales with the directional light (sun) since sky brightness is scattered sunlight.
    // The 0.3 factor is atmospheric turbidity — lower = hazier, higher = cleaner air.
    float sky_scale = u_lights.u_directional.intensity * 0.3;
    vec3  sky_tint  = mix(vec3(1.0), u_lights.u_directional.color, 0.5);
    float up        = max(ray_dir.y, 0.0);
    vec3 horizon    = vec3(0.80, 0.85, 0.90) * sky_tint * sky_scale;
    vec3 zenith     = vec3(0.20, 0.45, 0.85) * sky_tint * sky_scale;
    vec3 sky        = mix(horizon, zenith, pow(up, 0.6));

    // Ground: dark grey below the horizon
    if (ray_dir.y < 0.0)
        sky = mix(vec3(0.12, 0.10, 0.09) * sky_scale, horizon, exp(ray_dir.y * 8.0));

    // Sun disc and halo
    float sun_cos   = dot(ray_dir, sun_dir);
    float sun_disc  = smoothstep(0.9997, 0.9999, sun_cos);   // ~0.4° angular diameter
    float sun_halo  = pow(max(sun_cos, 0.0), 64.0) * 0.5;    // soft glow

    vec3 sun_color = u_lights.u_directional.color * u_lights.u_directional.intensity;
    sky += sun_color * (sun_disc + sun_halo);

    return sky;
}

vec3 sky_dome(vec3 ray_dir) {
    float sky_scale = u_lights.u_directional.intensity * 0.3;
    vec3  sky_tint  = mix(vec3(1.0), u_lights.u_directional.color, 0.5);
    float up        = max(ray_dir.y, 0.0);
    vec3  horizon   = vec3(0.80, 0.85, 0.90) * sky_tint * sky_scale;
    vec3  zenith    = vec3(0.20, 0.45, 0.85) * sky_tint * sky_scale;
    if (ray_dir.y < 0.0)
        return mix(vec3(0.12, 0.10, 0.09) * sky_scale, horizon, exp(ray_dir.y * 8.0));
    return mix(horizon, zenith, pow(up, 0.6));
}

void main() {
    vec3 ray_dir = normalize(gl_WorldRayDirectionEXT);
    payload.radiance       = sky_dome(ray_dir);
    payload.next_direction = vec3(0.0); // signals path termination to raygen
    payload.hit_normal     = vec3(0.0); // no surface — sky sentinel
    payload.hit_depth      = -1.0;
}