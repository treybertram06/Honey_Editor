
// Called from the composite shader - seemed like a convienent place to do so
float fxaa_luma(vec3 rgb) {
    // Most accurate method of calculation
    return dot(rgb, vec3(0.299, 0.587, 0.114)); // The numbers Mason...

    // Optimized calculation (Lottes, 2011)
    //return rgb.y * (0.587/0.299) + rgb.x; // Compiler turns this into a FMA
}

#define LUMA_AT(ox, oy) textureLodOffset(sampler2D(tex, smp), uv, 0.0, ivec2(ox, oy)).a

const int FXAA_STEPS = 12; // High detail level of steps, can be reduced to improve performance
const float FXAA_STEP_MUL[FXAA_STEPS] = float[](
    1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0
);

vec3 fxaa(texture2D tex, sampler smp, vec2 uv, vec2 rcp_frame, float subpix, float edge_thr, float edge_thr_min, int debug_view) {
    vec4 rgbl_mid = textureLod(sampler2D(tex, smp), uv, 0.0);
    float luma_mid = rgbl_mid.a;
    float luma_n = LUMA_AT(0.0, -1.0);
    float luma_s = LUMA_AT(0.0, 1.0);
    float luma_w = LUMA_AT(-1.0, 0.0);
    float luma_e = LUMA_AT(1.0, 0.0);

    float luma_min = min(luma_mid, min(min(luma_n, luma_s), min(luma_e, luma_w)));
    float luma_max = max(luma_mid, max(max(luma_n, luma_s), max(luma_e, luma_w)));
    float range = luma_max - luma_min;

    if (range < max(edge_thr_min, luma_max * edge_thr)) { // Early out
        return rgbl_mid.rgb;
    }

    if (debug_view == 1) { // Edge mask
        return mix(rgbl_mid.rgb, vec3(1.0, 0.0, 0.0), 0.6); // Return a red tinted edge mask
    }

    float luma_nw = LUMA_AT(-1.0, -1.0);
    float luma_ne = LUMA_AT(1.0, -1.0);
    float luma_sw = LUMA_AT(-1.0, 1.0);
    float luma_se = LUMA_AT(1.0, 1.0);

    // Find edge orientation
    float luma_ns = luma_n + luma_s;
    float luma_we = luma_w + luma_e;

    float edge_horiz =
        abs(luma_nw + luma_sw - 2.0 * luma_w) // left column
      + abs(luma_ns - 2.0 * luma_mid) * 2.0   // centre column, weighted 2x
      + abs(luma_ne + luma_se - 2.0 * luma_e);// right column

    float edge_vert =
    abs(luma_nw + luma_ne - 2.0 * luma_n)   // top row
    + abs(luma_we - 2.0 * luma_mid) * 2.0   // centre row, weighted 2x
    + abs(luma_sw + luma_se - 2.0 * luma_s);// bottom row

    bool is_horiz = edge_horiz >= edge_vert;

    if (debug_view == 2) { // Edge orientation mask
        vec3 tint = is_horiz ? vec3(1.0, 0.8, 0.0) : vec3(0.0, 0.6, 1.0); // yellow = horiz, blue = vert
        return mix(rgbl_mid.rgb, tint, 0.6);
    }

    // Which side of the edge?

    // The two neighbours of the edge, negative being left/up and positive being down/right
    float luma_neg = is_horiz ? luma_n : luma_w;
    float luma_pos = is_horiz ? luma_s : luma_e;

    // Size of one texel along the edge
    float step_len = is_horiz ? rcp_frame.y : rcp_frame.x;

    // Gradient (difference of the two pixels) in both directions. Determines which direction perpendicular to the axis this edge in on (e.x. Is this edge the top of a rooftop bordering the sky? The bottom of an overhang?)
    float grad_neg = abs(luma_neg - luma_mid);
    float grad_pos = abs(luma_pos - luma_mid);
    bool neg_is_steeper = grad_neg >= grad_pos;

    if (neg_is_steeper) step_len = -step_len; // edge is up/left

    // The walk's stop tolerance - walk stops when luma drifts a quarter of the edge's contrast away
    float grad_scaled = 0.25 * max(grad_neg, grad_pos); // 0.25 is because Lottes says so...

    float luma_across = neg_is_steeper ? luma_neg : luma_pos;
    float luma_local_avg = 0.5 * (luma_across + luma_mid); // The average of the current pixel and the one over the edge - acts as a "signature" for this edge. As we walk across, all pixels along this edge will share a luma_local_avg +/- grad_scaled

    // Move half a texel toward the edge..
    vec2 uv_edge = uv;
    if (is_horiz) uv_edge.y += 0.5 * step_len;
    else          uv_edge.x += 0.5 * step_len;

    if (debug_view == 3) { // Edge-side mask
        // Green = edge is up/left (negative), magenta = edge is down/right (positive)
        vec3 tint = step_len < 0.0 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 1.0);
        return mix(rgbl_mid.rgb, tint, 0.6);
    }

    // Now we walk along the edge...
    // walk vector
    vec2 along = is_horiz ? vec2(rcp_frame.x, 0.0)
                          : vec2(0.0, rcp_frame.y);

    vec2 pos_neg = uv_edge, pos_pos = uv_edge; // POSITION_(positive/negative)
    float end_neg = 0.0, end_pos = 0.0; // luma - localavg at each cursor point
    bool done_neg = false, done_pos = false;

    for (int i = 0; i < FXAA_STEPS; ++i) {
        if (!done_neg) {
            pos_neg -= along * FXAA_STEP_MUL[i];
            end_neg = textureLod(sampler2D(tex, smp), pos_neg, 0.0).a - luma_local_avg;
            done_neg = abs(end_neg) >= grad_scaled; // Is our end_neg outsite of our tolerance? If so, stop walking the edge
        }
        if (!done_pos) {
            pos_pos += along * FXAA_STEP_MUL[i];
            end_pos = textureLod(sampler2D(tex, smp), pos_pos, 0.0).a - luma_local_avg;
            done_pos = abs(end_pos) >= grad_scaled;
        }
        if (done_neg && done_pos) break;
    }

    float dist_neg = is_horiz ? (uv.x - pos_neg.x) : (uv.y - pos_neg.y);
    float dist_pos = is_horiz ? (pos_pos.x - uv.x) : (pos_pos.y - uv.y);

    //if (debug_view == 4) {
    //    float px = is_horz ? rcp_frame.x : rcp_frame.y;   // UV size of one texel along the edge
    //    return vec3(dist_neg / px, dist_pos / px, 0.0) / 16.0;
    //}

    // Distances -> edge offset
    bool neg_is_nearer = dist_neg < dist_pos;
    float dist_near = min(dist_neg, dist_pos);
    float span_len = dist_neg + dist_pos;

    // Which side of the edge's average are we? and which side is the nearer end?
    float end_near = neg_is_nearer ? end_neg : end_pos;
    bool mid_below = luma_mid < luma_local_avg;
    bool good_span = (end_near < 0.0) != mid_below;

    float edge_offset = good_span ? (0.5 - dist_near / span_len) : 0.0;

    //if (debug_view == 5) { // Should show a bright-to-black gradient that is brightest at the tip of the step
    //    return vec3(edge_offset * 2.0);  // 0 = black, full half-texel blend = white
    //}

    // Subpixel aliasing
    float luma_corners = luma_nw + luma_ne + luma_sw + luma_se;
    float subpix_avg = (2.0 * (luma_ns + luma_we) + luma_corners) * (1.0 / 12.0);

    float subpix_raw = clamp(abs(subpix_avg - luma_mid) / range, 0.0, 1.0);
    float subpix_shaped = smoothstep(0.0, 1.0, subpix_raw);
    float subpix_offset = subpix_shaped * subpix_shaped * subpix;

    float final_offset = max(edge_offset, subpix_offset);

    // Final sample
    vec2 final_uv = uv;
    if (is_horiz) final_uv.y += final_offset * step_len;
    else          final_uv.x += final_offset * step_len;

    return textureLod(sampler2D(tex, smp), final_uv, 0.0).rgb;
}

#undef LUMA_AT