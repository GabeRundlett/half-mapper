#pragma once

#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>

struct DrawVertex {
    f32vec3 pos;
    f32vec2 uv0;
    f32vec2 uv1;
};

struct GpuInput {
    f32mat4x4 mvp_mat;
};

DAXA_ENABLE_BUFFER_PTR(DrawVertex)
DAXA_ENABLE_BUFFER_PTR(GpuInput)

struct DrawPush {
    daxa_BufferPtr(GpuInput) gpu_input;
    daxa_BufferPtr(DrawVertex) vertices;
    daxa_ImageViewId image_id0;
    daxa_ImageViewId image_id1;
    daxa_SamplerId image_sampler0;
    daxa_SamplerId image_sampler1;
};

#define VERTICES(i) deref(daxa_push_constant.vertices[i])
#define INPUT deref(daxa_push_constant.gpu_input)
