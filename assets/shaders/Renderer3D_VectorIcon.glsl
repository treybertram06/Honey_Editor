#type vertex
#version 450

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_tint;
layout(location = 2) flat out int v_entity_id;
layout(location = 3) out vec4 v_fill_color;
layout(location = 4) out vec2 v_canvas_size;
layout(location = 5) out vec4 v_bbox;
layout(location = 6) flat out uint v_band_offset;
layout(location = 7) flat out uint v_band_count;


#include "global_bindings.glsli"
layout(set = HN_GLOBAL_SET, binding = HN_GBIND_CAMERA) uniform CameraUBO {
    mat4 u_ViewProjection;
    vec3 u_Position;
    float u_Exposure;
    mat4 u_InvViewProjection;
    mat4 u_View;
    mat4 u_Projection;
    mat4 u_InvProjection;
} u_Camera;

// Same gBuffer depth target the fragment stage samples for occlusion - reused here purely for its
// resolution (matches this pass's own output size), so ScreenSpace sizing needs no extra plumbing.
layout(set = 1, binding = 0) uniform texture2D u_gDepth;
layout(set = 1, binding = 1) uniform sampler   u_NearestSampler;

// Renderer3D::SizeMode (renderer_3d.h) — ScreenSpace = 0, WorldSpace = 1.
const uint SIZEMODE_SCREEN_SPACE = 0u;

// One instance per SlugIcon shape - mirrors Honey::GPUIconInstance (gpu_types.h) field-for-field.
struct GPUIconInstance {
    vec4 world_pos;  // xyz = world position, w = size
    vec4 tint;       // caller-supplied tint (submit_icon)
    vec4 fill_color; // this shape's own SVG fill color
    vec4 bbox;       // xy = shape bbox_min, zw = shape bbox_max (SVG units)

    vec2 canvas_size; // icon's overall canvas size (SVG units) - shared across an icon's shapes
    int  entity_id;
    uint sizemode;

    uint band_offset; // this shape's own band-table slice
    uint band_count;
};
layout(set = 1, binding = 2) readonly buffer IconInstances {
    GPUIconInstance data[];
} u_IconInstances;

void main() {
    GPUIconInstance inst = u_IconInstances.data[gl_InstanceIndex];

    // Calculate UVs for Triangle Strip (Vertices: 0, 1, 2, 3)
    // 0 = (0,0), 1 = (0,1), 2 = (1,0), 3 = (1,1)
    uint corner = gl_VertexIndex & 3u;
    v_uv = vec2(float((corner & 2u) >> 1u), float(corner & 1u));

    // Convert UVs to local quad coordinates centered at origin [-0.5, 0.5]
    vec2 local_pos = (v_uv - vec2(0.5));

    vec3 camera_right = vec3(u_Camera.u_View[0][0], u_Camera.u_View[1][0], u_Camera.u_View[2][0]);
    vec3 camera_up    = vec3(u_Camera.u_View[0][1], u_Camera.u_View[1][1], u_Camera.u_View[2][1]);

    float size = inst.world_pos.w; // WorldSpace: literal world-space size, unaffected by distance.
    if (inst.sizemode == SIZEMODE_SCREEN_SPACE) {
        // "size" is a desired constant on-screen pixel size instead. Convert it to the
        // world-space size that projects to that many pixels at this icon's distance from the
        // camera: pixel_height = (world_height * proj[1][1] / view_z) * (viewport_height / 2),
        // solved for world_height. See Renderer3D_DeferredLighting.glsl for the same
        // abs(view-space z) idiom used for view_z here.
        float view_z          = abs((u_Camera.u_View * vec4(inst.world_pos.xyz, 1.0)).z);
        float viewport_height = float(textureSize(sampler2D(u_gDepth, u_NearestSampler), 0).y);
        float fov_scale        = u_Camera.u_Projection[1][1]; // = 1 / tan(fovY / 2)

        float desired_px_size = inst.world_pos.w;
        size = (desired_px_size * view_z * 2.0) / (fov_scale * viewport_height);
    }

    vec3 world_pos = inst.world_pos.xyz
    + (camera_right * local_pos.x * size)
    + (camera_up * local_pos.y * size);

    // Calculate final clip position
    gl_Position = u_Camera.u_Projection * u_Camera.u_View * vec4(world_pos, 1.0);

    // Pass instance attributes to fragment shader - all flat/constant across one shape's quad
    // except v_uv, which the fragment stage re-maps into this shape's own SVG-unit space.
    v_tint        = inst.tint;
    v_entity_id   = inst.entity_id;
    v_fill_color  = inst.fill_color;
    v_canvas_size = inst.canvas_size;
    v_bbox        = inst.bbox;
    v_band_offset = inst.band_offset;
    v_band_count  = inst.band_count;
}

#type fragment
#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_tint;
layout(location = 2) flat in int v_entity_id;
layout(location = 3) in vec4 v_fill_color;
layout(location = 4) in vec2 v_canvas_size;
layout(location = 5) in vec4 v_bbox;
layout(location = 6) flat in uint v_band_offset;
layout(location = 7) flat in uint v_band_count;

layout(location = 0) out vec4 o_color;
layout(location = 1) out int  o_entity_id;

layout(set = 1, binding = 0) uniform texture2D u_gDepth;
layout(set = 1, binding = 1) uniform sampler   u_NearestSampler;

// Mirror of the C++ BandEntry / QuadBezier structs (std430 layout matches the raw
// uint32_t / glm::vec2 members in slug_font.h exactly).
struct BandEntry {
    uint curve_offset;
    uint curve_count;
};

struct QuadBezier {
    vec2 p0;
    vec2 p1;
    vec2 p2;
};

#include "global_bindings.glsli"
layout(set = HN_GLOBAL_SET, binding = HN_GBIND_ICON_BAND_TABLE) readonly buffer IconBandTableBuffer {
    BandEntry u_band_table[];
};
layout(set = HN_GLOBAL_SET, binding = HN_GBIND_ICON_CURVES) readonly buffer IconCurveBuffer {
    QuadBezier u_curves[];
};

// Signed winding contribution of one quadratic Bezier at the current fragment.
// p0/p1/p2 are pre-translated so the fragment sits at the origin.
// px_size is the pixel footprint in SVG units (used for sub-pixel AA).
float bezier_winding(vec2 p0, vec2 p1, vec2 p2, float px_size) {
    // Coefficients of the quadratic y(t) = a*t^2 + 2*b*t + c
    float a = p0.y - 2.0*p1.y + p2.y;
    float b = p1.y - p0.y;
    float c = p0.y;

    float winding = 0.0;

    if (abs(a) < 0.001) {
        // Degenerate — treat as linear
        if (abs(b) < 0.001) return 0.0;
        float t = -c / (2.0 * b);
        if (t >= 0.0 && t < 1.0) {
            float xt = (1.0-t)*(1.0-t)*p0.x + 2.0*t*(1.0-t)*p1.x + t*t*p2.x;
            float aa  = clamp(0.5 + xt / px_size, 0.0, 1.0);
            winding  += sign(2.0 * b) * aa;
        }
    } else {
        float disc = b*b - a*c;
        if (disc < 0.0) return 0.0;
        float sq = sqrt(disc);

        // Two potential crossings; use half-open [0,1) to avoid
        // double-counting the shared endpoint where curves meet.
        for (int i = 0; i < 2; ++i) {
            float t = (i == 0) ? (-b - sq) / a : (-b + sq) / a;
            if (t >= 0.0 && t < 1.0) {
                float xt = (1.0-t)*(1.0-t)*p0.x + 2.0*t*(1.0-t)*p1.x + t*t*p2.x;
                float aa  = clamp(0.5 + xt / px_size, 0.0, 1.0);
                winding  += sign(2.0*(a*t + b)) * aa;
            }
        }
    }

    return winding;
}

void main() {
    // gBuffer's depth target is full swapchain resolution, same as this
    // pass's own targets, so this fragment's screen UV can be recovered
    // straight from its window-space coordinate without a resolution uniform.
    vec2 depth_size = vec2(textureSize(sampler2D(u_gDepth, u_NearestSampler), 0));
    vec2 screen_uv  = gl_FragCoord.xy / depth_size;

    float scene_depth = texture(sampler2D(u_gDepth, u_NearestSampler), screen_uv).r;

#if 1 // TEMP DEBUG - remove once occlusion is confirmed working
    // Binary pass/fail of the exact same comparison used for the real discard below - avoids
    // raw-depth-value visualization being useless due to standard-Z's precision compression
    // (everything past a few meters reads near-1.0 to the eye either way).
    bool occluded = gl_FragCoord.z > scene_depth;
    o_color = occluded ? vec4(1.0, 0.0, 0.0, 1.0)  // RED = this pixel thinks it's occluded
                        : vec4(0.0, 1.0, 0.0, 1.0); // GREEN = this pixel thinks it's visible
    o_entity_id = v_entity_id;
    return;
#endif

    // Standard (non-reversed) depth: smaller = nearer. If real scene geometry
    // at this pixel is closer to the camera than this icon fragment, the
    // icon is occluded. This is the manual stand-in for hardware depthTest
    // now that this pass has no bound depth attachment of its own.
    if (gl_FragCoord.z > scene_depth)
        discard;

    // Recover this fragment's position in the icon's SVG canvas space. v_uv spans the whole
    // quad, which corresponds to the icon's whole canvas (0,0)-(canvas_size), shared by every
    // shape of this icon - NOT this shape's own (tighter) bbox, so sibling shapes stay aligned.
    vec2 frag_font = v_uv * v_canvas_size;

    // This shape's own bbox (not the shared canvas) scopes both the early reject and the band
    // lookup below - it's what build_shape() (slug_icon.cpp) actually banded curves against.
    vec2 bbox_min = v_bbox.xy;
    vec2 bbox_max = v_bbox.zw;
    if (any(lessThan(frag_font, bbox_min)) || any(greaterThan(frag_font, bbox_max)))
        discard;

    // Pixel footprint in SVG units — drives the AA width.
    float px_size = fwidth(frag_font.x);

    // Select the band this fragment falls in (band 0 = min-y, matches build_shape).
    float height  = max(bbox_max.y - bbox_min.y, 1e-4);
    float norm_y  = (frag_font.y - bbox_min.y) / height;
    int band_idx  = clamp(int(norm_y * float(v_band_count)), 0, int(v_band_count) - 1);

    BandEntry band = u_band_table[v_band_offset + uint(band_idx)];

    // Accumulate winding contributions from every curve in the band.
    float winding = 0.0;
    for (uint i = 0u; i < band.curve_count; ++i) {
        QuadBezier curve = u_curves[band.curve_offset + i];
        winding += bezier_winding(curve.p0 - frag_font,
                                  curve.p1 - frag_font,
                                  curve.p2 - frag_font,
                                  px_size);
    }

    // Non-zero fill rule; coverage is smooth thanks to the per-crossing AA.
    float coverage = clamp(abs(winding), 0.0, 1.0);
    if (coverage <= 0.0)
        discard;

    o_color     = vec4(v_fill_color.rgb * v_tint.rgb, v_fill_color.a * v_tint.a * coverage);
    o_entity_id = v_entity_id;
}