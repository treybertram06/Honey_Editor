

vec3 oct_decode(vec2 p) {
    vec3 n = vec3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0,
        n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}


// Geometric specular antialiasing (Tokuyoshi & Kaplanyan 2019, "Improved Geometric Specular
// Antialiasing"). Widens the GGX lobe by the screen-space variance of the shading normal, so a
// pixel whose footprint spans a sharply curving / bumpy normal gets a highlight as wide as that
// spread instead of a sub-pixel spike that blinks on and off as the camera moves.
// Must run in the geometry pass on the final shading normal: in the full-screen lighting pass the
// 2x2 derivative quads straddle silhouettes and would roughen every geometric edge.
// Takes and returns perceptual roughness; the filtering itself happens on alpha^2.
float specular_aa_roughness(vec3 N, float roughness) {
    const float SIGMA2 = 0.25; // variance of the pixel filter kernel
    const float KAPPA  = 0.18; // cap on added alpha^2 so smooth-but-curved surfaces don't go matte
    vec3  dndu   = dFdx(N);
    vec3  dndv   = dFdy(N);
    float kernel = min(2.0 * SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv)), KAPPA);
    float a      = roughness * roughness;
    float a2     = clamp(a * a + kernel, 0.0, 1.0);
    return sqrt(sqrt(a2));
}
