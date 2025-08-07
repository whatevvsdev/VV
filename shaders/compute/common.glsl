
struct Ray // 32 Bytes
{
    vec3 position;
    vec3 direction;
};

struct IntersectResult // 48 Bytes
{
    float hit_distance;
    float dummy[3];
    vec3 incoming_direction;
    vec3 normal;
};

vec3 get_translation_from_matrix(mat4 matrix)
{
    return matrix[3].xyz;
}

#define FLT_MAX (1.0 / 0.0)
#define EPSILON 0.001f