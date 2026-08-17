#version 450

layout(push_constant) uniform CursorPush {
    vec2 screen_size;
    vec2 rect_min;
    vec2 rect_max;
    vec2 p0;
    vec2 p1;
    vec2 half_size;
    vec4 color;
    float softness;
    float intensity;
    float mode;
    float _pad;
} push;

layout(location = 0) out vec2 frag_pos;

void main() {
    vec2 corners[6] = vec2[](
        vec2(push.rect_min.x, push.rect_min.y),
        vec2(push.rect_max.x, push.rect_min.y),
        vec2(push.rect_max.x, push.rect_max.y),
        vec2(push.rect_min.x, push.rect_min.y),
        vec2(push.rect_max.x, push.rect_max.y),
        vec2(push.rect_min.x, push.rect_max.y)
    );
    vec2 pos = corners[gl_VertexIndex];
    vec2 ndc = vec2(
        (pos.x / push.screen_size.x) * 2.0 - 1.0,
        (pos.y / push.screen_size.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_pos = pos;
}
