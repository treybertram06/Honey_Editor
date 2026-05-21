
struct HitRecord {
    vec2  barycentrics;
    float hit_t;
    uint  instance_id;       // gl_InstanceCustomIndexEXT → geometry + material lookup
    uint  primitive_id;
    uint  hit_kind;          // 0 = miss, 1 = hit
    float _pad0;
    float _pad1;
    // World-space interpolated vertex normal and tangent, written by ClosestHit.
    // Material.comp needs these but has no access to gl_ObjectToWorldEXT.
    vec4  shading_normal;    // xyz = world-space vertex normal (interpolated), w = unused
    vec4  tangent;           // xyz = world-space tangent, w = bitangent sign (+1 or -1)
};