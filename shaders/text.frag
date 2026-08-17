#version 450

layout(set = 0, binding = 0) uniform sampler2D font_atlas;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;
layout(location = 2) in float frag_use_tex;

layout(location = 0) out vec4 out_color;

void main() {
    float alpha = frag_use_tex > 0.5 ? texture(font_atlas, frag_uv).r : 1.0;
    out_color = vec4(frag_color.rgb, frag_color.a * alpha);
}
