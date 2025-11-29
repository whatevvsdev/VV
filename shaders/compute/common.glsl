
struct Ray // 32 Bytes
{
    vec3 position;
    vec3 direction;
};

struct IntersectResult // 32 Bytes
{
    vec4 incoming_direction_and_hit_distance;
    vec4 normal;
};

vec3 get_translation_from_matrix(mat4 matrix)
{
    return matrix[3].xyz;
}

#define FLT_MAX (1.0 / 0.0)
#define EPSILON 0.001f

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require