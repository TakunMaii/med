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

layout(location = 0) in vec2 frag_pos;
layout(location = 0) out vec4 out_color;

float rect_alpha(vec2 p, vec2 center, vec2 half_size, float softness) {
    vec2 d = abs(p - center) - half_size;
    float outside = length(max(d, vec2(0.0)));
    float inside = min(max(d.x, d.y), 0.0);
    float dist = outside + inside;
    return 1.0 - smoothstep(0.0, softness, dist);
}

float tapered_trail_alpha(vec2 p, vec2 a, vec2 b, vec2 half_size, float softness) {
    vec2 ab = b - a;
    float len = max(length(ab), 0.0001);
    vec2 axis = ab / len;
    vec2 perp = vec2(-axis.y, axis.x);
    float along = dot(p - a, axis);
    float t = along / len;
    float cross = abs(dot(p - a, perp));
    float base_width = abs(perp.x) * half_size.x + abs(perp.y) * half_size.y;
    if (push.mode > 0.5) base_width = max(base_width, max(half_size.x * 1.8, 2.0));
    float width = max(base_width * clamp(t, 0.0, 1.0), 0.35);
    float side = 1.0 - smoothstep(width, width + softness, cross);
    float head = smoothstep(-softness, 0.0, along);
    float tail = 1.0 - smoothstep(len, len + softness, along);
    return side * head * tail;
}

void main() {
    float trail = tapered_trail_alpha(frag_pos, push.p0, push.p1, push.half_size, push.softness) * push.intensity;
    float body = rect_alpha(frag_pos, push.p1, push.half_size, max(push.softness * 0.35, 1.0));
    float alpha = max(trail, body);
    if (alpha <= 0.001) discard;
    out_color = vec4(push.color.rgb, push.color.a * alpha);
}
