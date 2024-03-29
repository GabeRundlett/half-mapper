#pragma once

#define DAXA_ENABLE_IMAGE_OVERLOADS_BASIC 1
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

DAXA_DECL_BUFFER_PTR(DrawVertex)
DAXA_DECL_BUFFER_PTR(GpuInput)

struct DrawPush {
    daxa_BufferPtr(GpuInput) gpu_input;
    daxa_BufferPtr(DrawVertex) vertices;
    daxa_ImageViewId image_id0;
    daxa_ImageViewId image_id1;
    daxa_SamplerId image_sampler0;
    daxa_SamplerId image_sampler1;
    f32vec3 offset;
};

#define VERTICES(i) deref(push.vertices[i])
#define INPUT deref(push.gpu_input)
