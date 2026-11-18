// vertex_decode.glsl — decode helpers for the compressed VertexPBR layout.
// Include in any shader that reads from a vertex buffer with VERTEX_STRIDE=6.
//
// Buffer layout (per vertex, 24 bytes / 6 uint32s):
//   [0-2]  position       (3×f32, read directly)
//   [3]    normal_packed  (oct-encoded, packSnorm2x16)
//   [4]    tangent_packed (oct xyz in bits 0-30, bitangent sign in bit 31)
//   [5]    uv0_packed     (packHalf2x16)

// Inverse of the oct-encode used on the CPU.
vec3 oct_decode(vec2 e) {
    vec3 v = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    return normalize(v);
}

// Unpack a normal stored as packSnorm2x16(oct_encode(n)).
vec3 unpack_normal(uint packed) {
    int xi = int(packed & 0xFFFFu);
    xi = (xi >= 0x8000) ? xi - 0x10000 : xi;
    int yi = int((packed >> 16u) & 0xFFFFu);
    yi = (yi >= 0x8000) ? yi - 0x10000 : yi;
    return oct_decode(vec2(float(xi) / 32767.0, float(yi) / 32767.0));
}

// Unpack a tangent stored as: oct x in bits 0-15 (snorm16), oct y in bits 16-30 (snorm15),
// bitangent sign in bit 31 (0=+1, 1=-1). Returns vec4(tangent_xyz, bitangent_sign).
vec4 unpack_tangent(uint packed) {
    int xi = int(packed & 0xFFFFu);
    xi = (xi >= 0x8000) ? xi - 0x10000 : xi;
    int yi = int((packed >> 16u) & 0x7FFFu);
    yi = (yi >= 0x4000) ? yi - 0x8000 : yi;
    float bitan_sign = ((packed >> 31u) != 0u) ? -1.0 : 1.0;
    return vec4(oct_decode(vec2(float(xi) / 32767.0, float(yi) / 16383.0)), bitan_sign);
}

// Read a packed normal from a float[] vertex buffer (reinterpret bits as uint).
vec3 vb_unpack_normal(float packed_as_float) {
    return unpack_normal(floatBitsToUint(packed_as_float));
}

// Read a packed tangent from a float[] vertex buffer.
vec4 vb_unpack_tangent(float packed_as_float) {
    return unpack_tangent(floatBitsToUint(packed_as_float));
}

// Read packed UV from a float[] vertex buffer.
vec2 vb_unpack_uv(float packed_as_float) {
    return unpackHalf2x16(floatBitsToUint(packed_as_float));
}
