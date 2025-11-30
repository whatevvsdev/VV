
struct Ray // 32 Bytes
{
    vec3 position;
    vec3 direction;
};

struct IntersectResult // 32 Bytes
{
    vec4 incoming_direction_and_hit_distance;
    vec4 normal; // last element of vec4 stores debug gpu time
};

vec3 get_translation_from_matrix(mat4 matrix)
{
    return matrix[3].xyz;
}

#define FLT_MAX (1.0 / 0.0)
#define EPSILON 0.001f

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shader_clock : require

vec3 viridis_quintic( float x )
{
    x = clamp(x, 0.0f, 1.0f);
    vec4 x1 = vec4( 1.0, x, x * x, x * x * x ); // 1 x x2 x3
    vec4 x2 = x1 * x1.w * x; // x4 x5 x6 x7
    return ( vec3(
                     dot( x1.xyzw, vec4( +0.280268003, -0.143510503, +2.225793877, -14.815088879 ) ) + dot( x2.xy, vec2( +25.212752309, -11.772589584 ) ),
                     dot( x1.xyzw, vec4( -0.002117546, +1.617109353, -1.909305070, +2.701152864 ) ) + dot( x2.xy, vec2( -1.685288385, +0.178738871 ) ),
                     dot( x1.xyzw, vec4( +0.300805501, +2.614650302, -12.019139090, +28.933559110 ) ) + dot( x2.xy, vec2( -33.491294770, +13.762053843 ) ) ) );
}