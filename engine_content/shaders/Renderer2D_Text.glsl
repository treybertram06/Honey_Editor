#type vertex
#version 450

layout (location = 0) in vec2 a_local_pos;     // static quad corners (±0.5)
layout (location = 1) in vec2 a_local_tex;     // static UVs [0,1]

layout (location = 2) in vec3  i_center;               // per-instance world-space centre
layout (location = 3) in vec2  i_half_size;             // world-space half-extents
layout (location = 4) in float i_rotation;              // Z rotation in radians
layout (location = 5) in vec4  i_color;
layout (location = 6) in vec2  i_bbox_min;              // glyph bbox min in font units
layout (location = 7) in vec2  i_bbox_size;             // glyph bbox size in font units
layout (location = 8) in uint  i_band_table_offset;
layout (location = 9) in uint  i_num_bands;
layout (location = 10) in int  i_entity_id;

#ifdef HN_VULKAN
layout (set = 0, binding = 0, std140) uniform camera {
    mat4 u_view_projection;
};
#else
layout (std140, binding = 0) uniform camera {
    mat4 u_view_projection;
};
#endif

layout(location=0) out vec4  v_color;
layout(location=1) out vec2  v_local_tex;
layout(location=2) out vec2  v_bbox_min;
layout(location=3) out vec2  v_bbox_size;
layout(location=4) flat out uint v_band_table_offset;
layout(location=5) flat out uint v_num_bands;
layout(location=6) flat out int  v_entity_id;

void main()
{
    vec2 p = a_local_pos * i_half_size * 2.0;

    float cos_r = cos(i_rotation);
    float sin_r = sin(i_rotation);
    vec2 p_rot = vec2(p.x * cos_r - p.y * sin_r,
                      p.x * sin_r + p.y * cos_r);

    vec3 world_pos = i_center + vec3(p_rot, 0.0);

    v_color             = i_color;
    v_local_tex         = a_local_tex;
    v_bbox_min          = i_bbox_min;
    v_bbox_size         = i_bbox_size;
    v_band_table_offset = i_band_table_offset;
    v_num_bands         = i_num_bands;
    v_entity_id         = i_entity_id;

    gl_Position = u_view_projection * vec4(world_pos, 1.0);
}

#type fragment
#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out int  entity_id;

layout(location=0) in vec4  v_color;
layout(location=1) in vec2  v_local_tex;
layout(location=2) in vec2  v_bbox_min;
layout(location=3) in vec2  v_bbox_size;
layout(location=4) flat in uint v_band_table_offset;
layout(location=5) flat in uint v_num_bands;
layout(location=6) flat in int  v_entity_id;

// Mirror of the C++ BandEntry / QuadBezier structs (std430 layout matches
// the raw uint32_t / glm::vec2 members in slug_font.h exactly).
struct BandEntry {
    uint curve_offset;
    uint curve_count;
};

struct QuadBezier {
    vec2 p0;
    vec2 p1;
    vec2 p2;
};

#ifdef HN_VULKAN
layout (set = 1, binding = 1, std430) readonly buffer BandTableBuffer {
    BandEntry u_band_table[];
};
layout (set = 1, binding = 2, std430) readonly buffer CurveBuffer {
    QuadBezier u_curves[];
};
#else
layout (std430, binding = 1) readonly buffer BandTableBuffer {
    BandEntry u_band_table[];
};
layout (std430, binding = 2) readonly buffer CurveBuffer {
    QuadBezier u_curves[];
};
#endif

// Signed winding contribution of one quadratic Bezier at the current fragment.
// p0/p1/p2 are pre-translated so the fragment sits at the origin.
// px_size is the pixel footprint in font units (used for sub-pixel AA).
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

void main()
{
    // Reconstruct this fragment's position in unscaled font units.
    // v_local_tex is [0,1] across the quad, mapping [bbox_min, bbox_max].
    vec2 frag_font = v_bbox_min + v_local_tex * v_bbox_size;

    // Pixel footprint in font units — drives the AA width.
    float px_size = fwidth(frag_font.x);

    // Select the band this fragment falls in (band 0 = min-y, matches build_glyph).
    // Derive from frag_font.y so it is correct for both text (positive bbox_size.y)
    // and Y-flipped icons (negative bbox_size.y passed from draw_icon).
    float bbox_y_min_svg = min(v_bbox_min.y, v_bbox_min.y + v_bbox_size.y);
    float height_svg     = abs(v_bbox_size.y);
    float norm_y         = (frag_font.y - bbox_y_min_svg) / height_svg;
    int band_idx = clamp(int(norm_y * float(v_num_bands)), 0, int(v_num_bands) - 1);

    BandEntry band = u_band_table[v_band_table_offset + uint(band_idx)];

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

    outColor  = vec4(v_color.rgb, v_color.a * coverage);
    entity_id = v_entity_id;
}