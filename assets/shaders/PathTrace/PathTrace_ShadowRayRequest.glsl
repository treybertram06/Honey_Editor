

struct ShadowRayRequest {
    vec3  origin;
    vec3  direction;
    float t_max;
    uint  path_index;
    uint  light_index;
};

