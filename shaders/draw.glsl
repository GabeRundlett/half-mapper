#include <shared/shared.inl>

DAXA_USE_PUSH_CONSTANT(DrawPush)

#if defined(DRAW_VERT)

layout(location = 0) out f32vec4 v_col;
void main() {
    DrawVertex vert = VERTICES(gl_VertexIndex);
    gl_Position = INPUT.mvp_mat * f32vec4(-(vert.pos + daxa_push_constant.offset), 1.0);
    v_col = f32vec4(vert.uv0, vert.uv1);
}

#elif defined(DRAW_FRAG)

layout(location = 0) in f32vec4 v_col;
layout(location = 0) out f32vec4 color;
void main() {
    f32vec2 uv0 = v_col.xy;
    f32vec2 uv1 = v_col.zw;
    f32vec4 tex0_col = texture(daxa_push_constant.image_id0, daxa_push_constant.image_sampler0, uv0);
    f32vec4 tex1_col = texture(daxa_push_constant.image_id1, daxa_push_constant.image_sampler1, uv1);

    color = f32vec4(tex0_col.rgb * tex1_col.rgb, 1);
    // color = f32vec4(tex0_col.rgb, 1);
    // color = f32vec4(tex1_col.rgb, 1);
}

#endif
