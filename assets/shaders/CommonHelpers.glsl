

vec3 oct_decode(vec2 p) {
    vec3 n = vec3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0,
        n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

