#include "utils/base_app.hpp"

#include "utils/player.hpp"

#include <span>
#include <glm/gtc/type_ptr.hpp>

#include "common.hpp"
#include "wad.hpp"
#include "bsp.hpp"
#include "ConfigXML.hpp"

struct HalfLife {
    ConfigXML *xmlconfig = new ConfigXML();
    std::vector<BSP *> maps;
    daxa::Device &device;

    HalfLife(daxa::Device &a_device) : device{a_device} {
        xmlconfig->LoadProgramConfig();
        xmlconfig->LoadMapConfig("halflife.xml");

        // Texture loading
        for (size_t i = 0; i < xmlconfig->m_vWads.size(); i++) {
            if (wad_load(device, xmlconfig->m_szGamePaths, xmlconfig->m_vWads[i] + ".wad") == -1) {
                return;
            }
        }

        // Map loading

        int mapCount = 0;
        int mapRenderCount = 0;
        int totalTris = 0;

        for (unsigned int i = 0; i < xmlconfig->m_vChapterEntries.size(); i++) {
            for (unsigned int j = 0; j < xmlconfig->m_vChapterEntries[i].m_vMapEntries.size(); j++) {
                ChapterEntry const sChapterEntry = xmlconfig->m_vChapterEntries[i];
                MapEntry const sMapEntry = xmlconfig->m_vChapterEntries[i].m_vMapEntries[j];

                if (sChapterEntry.m_bRender && sMapEntry.m_bRender) {
                    BSP *b = new BSP(device, xmlconfig->m_szGamePaths, "maps/" + sMapEntry.m_szName + ".bsp", sMapEntry);
                    b->SetChapterOffset(sChapterEntry.m_fOffsetX, sChapterEntry.m_fOffsetY, sChapterEntry.m_fOffsetZ);
                    totalTris += b->totalTris;
                    maps.push_back(b);
                    mapRenderCount++;
                }

                mapCount++;
            }
        }

        std::cout << mapCount << " maps found in config file." << std::endl;
        std::cout << "Total triangles: " << totalTris << std::endl;
    }

    ~HalfLife() {
        for (auto &[key, tex] : textures) {
            device.destroy_image(tex.image_id);
        }
        for (auto &map : maps) {
            device.destroy_image(map->lmap_image_id);
            for (usize i = 0; i < map->texturedTris.size(); ++i) {
                auto &buf = map->bufObjects[i];
                device.destroy_buffer(buf.buffer_id);
            }
            device.destroy_sampler(map->image_sampler);
        }
    }

    void render(daxa::CommandList &cmd_list, daxa::BufferId gpu_input_buffer) {
        for (auto &map : maps) {
            map->render(device, cmd_list, gpu_input_buffer);
        }
    }
};

struct App : BaseApp<App> {
    // clang-format off
    daxa::RasterPipeline draw_raster_pipeline = pipeline_compiler.create_raster_pipeline({
        .vertex_shader_info = {.source = daxa::ShaderFile{"draw.glsl"}, .compile_options = {.defines = {daxa::ShaderDefine{"DRAW_VERT"}}}},
        .fragment_shader_info = {.source = daxa::ShaderFile{"draw.glsl"}, .compile_options = {.defines = {daxa::ShaderDefine{"DRAW_FRAG"}}}},
        .color_attachments = {{.format = swapchain.get_format()}},
        .depth_test = {
            .depth_attachment_format = daxa::Format::D24_UNORM_S8_UINT,
            .enable_depth_test = true,
            .enable_depth_write = true,
        },
        .raster = {
            .face_culling = daxa::FaceCullFlagBits::BACK_BIT,
        },
        .push_constant_size = sizeof(DrawPush),
        .debug_name = APPNAME_PREFIX("draw_raster_pipeline"),
    }).value();
    // clang-format on
    daxa::BufferId gpu_input_buffer = device.create_buffer(daxa::BufferInfo{
        .size = sizeof(GpuInput),
        .debug_name = APPNAME_PREFIX("gpu_input_buffer"),
    });
    daxa::TaskBufferId task_gpu_input_buffer;
    daxa::BufferId vertex_buffer = device.create_buffer(daxa::BufferInfo{
        .size = sizeof(DrawVertex) * 3,
        .debug_name = APPNAME_PREFIX("vertex_buffer"),
    });
    daxa::TaskBufferId task_vertex_buffer;
    daxa::ImageId depth_image = device.create_image({
        .format = daxa::Format::D24_UNORM_S8_UINT,
        .aspect = daxa::ImageAspectFlagBits::DEPTH | daxa::ImageAspectFlagBits::STENCIL,
        .size = {size_x, size_y, 1},
        .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
    });
    daxa::TaskImageId task_depth_image;

    GpuInput gpu_input = {};
    Player3D player = {
        .pos = {0.0f, 0.0f, -1.0f},
        .rot = {0.0f, 0.0f, 0.0f},
    };
    bool paused = true;

    HalfLife halflife = HalfLife(device);

    daxa::TaskList loop_task_list = record_loop_task_list();

    ~App() {
        device.wait_idle();
        device.collect_garbage();
        device.destroy_buffer(gpu_input_buffer);
        device.destroy_buffer(vertex_buffer);
        device.destroy_image(depth_image);
    }
    void ui_update() {
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (paused) {
            ImGui::ShowDemoWindow();

            ImGui::Begin("Images");
            u32 i = 0;
            for (auto &[key, tex] : textures) {
                if ((i % 12) != 0)
                    ImGui::SameLine();
                ++i;
                ImGui::Image(*reinterpret_cast<ImTextureID const *>(&tex.image_id), ImVec2(tex.w, tex.h));
            }
            for (auto &map : halflife.maps) {
                ImGui::Image(*reinterpret_cast<ImTextureID const *>(&map->lmap_image_id), {1024, 1024});
            }
            ImGui::End();
        }
        ImGui::Render();
    }
    void on_update() {
        reload_pipeline(draw_raster_pipeline);
        ui_update();

        player.camera.resize(static_cast<i32>(size_x), static_cast<i32>(size_y));
        player.camera.set_pos(player.pos);
        player.camera.set_rot(player.rot.x, player.rot.y);
        player.update(delta_time);

        loop_task_list.remove_runtime_image(task_swapchain_image, swapchain_image);
        swapchain_image = swapchain.acquire_next_image();
        loop_task_list.add_runtime_image(task_swapchain_image, swapchain_image);
        if (swapchain_image.is_empty())
            return;

        auto mat = player.camera.get_vp();
        gpu_input.mvp_mat = daxa::math_operators::mat_from_span<f32, 4, 4>(std::span<f32, 4 * 4>{glm::value_ptr(mat), 4 * 4});
        loop_task_list.execute();

        std::cout << std::flush;
    }
    void on_mouse_move(f32 x, f32 y) {
        if (!paused) {
            f32 center_x = static_cast<f32>(size_x / 2);
            f32 center_y = static_cast<f32>(size_y / 2);
            auto offset = f32vec2{x - center_x, center_y - y};
            player.on_mouse_move(offset.x, offset.y);
            set_mouse_pos(center_x, center_y);
        }
    }
    void on_mouse_button(i32, i32) {}
    void on_key(i32 key_id, i32 action) {
        auto &io = ImGui::GetIO();
        if (io.WantCaptureKeyboard)
            return;
        if (key_id == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            toggle_pause();
        if (!paused) {
            player.on_key(key_id, action);
        }
    }
    void on_resize(u32 sx, u32 sy) {
        minimized = (sx == 0 || sy == 0);
        if (!minimized) {
            swapchain.resize();
            size_x = swapchain.get_surface_extent().x;
            size_y = swapchain.get_surface_extent().y;

            loop_task_list.remove_runtime_image(task_depth_image, depth_image);
            device.destroy_image(depth_image);
            depth_image = device.create_image({
                .format = daxa::Format::D24_UNORM_S8_UINT,
                .aspect = daxa::ImageAspectFlagBits::DEPTH | daxa::ImageAspectFlagBits::STENCIL,
                .size = {size_x, size_y, 1},
                .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
                .debug_name = APPNAME_PREFIX("depth_image"),
            });
            loop_task_list.add_runtime_image(task_depth_image, depth_image);

            on_update();
        }
    }
    void toggle_pause() {
        set_mouse_capture(paused);
        paused = !paused;
    }
    void record_tasks(daxa::TaskList &new_task_list) {
        task_depth_image = new_task_list.create_task_image({.debug_name = APPNAME_PREFIX("task_depth_image")});
        new_task_list.add_runtime_image(task_depth_image, depth_image);

        task_vertex_buffer = new_task_list.create_task_buffer({.debug_name = APPNAME_PREFIX("task_vertex_buffer")});
        new_task_list.add_runtime_buffer(task_vertex_buffer, vertex_buffer);

        new_task_list.add_task({
            .used_buffers = {
                {task_vertex_buffer, daxa::TaskBufferAccess::TRANSFER_WRITE},
            },
            .task = [this](daxa::TaskRuntime runtime) {
                auto cmd_list = runtime.get_command_list();
                auto gpu_input_staging_buffer = device.create_buffer({
                    .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                    .size = sizeof(GpuInput),
                    .debug_name = APPNAME_PREFIX("gpu_input_staging_buffer"),
                });
                cmd_list.destroy_buffer_deferred(gpu_input_staging_buffer);
                auto gpu_input_buffer_ptr = device.get_host_address_as<GpuInput>(gpu_input_staging_buffer);
                *gpu_input_buffer_ptr = gpu_input;
                cmd_list.copy_buffer_to_buffer({
                    .src_buffer = gpu_input_staging_buffer,
                    .dst_buffer = gpu_input_buffer,
                    .size = sizeof(GpuInput),
                });
            },
            .debug_name = APPNAME_PREFIX("Upload input"),
        });
        new_task_list.add_task({
            .used_buffers = {
                {task_vertex_buffer, daxa::TaskBufferAccess::TRANSFER_WRITE},
            },
            .task = [this](daxa::TaskRuntime runtime) {
                auto cmd_list = runtime.get_command_list();
                auto vertex_staging_buffer = device.create_buffer({
                    .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                    .size = sizeof(DrawVertex) * 3,
                    .debug_name = APPNAME_PREFIX("vertex_staging_buffer"),
                });
                cmd_list.destroy_buffer_deferred(vertex_staging_buffer);
                auto buffer_ptr = device.get_host_address_as<DrawVertex>(vertex_staging_buffer);
                *buffer_ptr = DrawVertex{{-0.5f, +0.5f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
                ++buffer_ptr;
                *buffer_ptr = DrawVertex{{+0.5f, +0.5f, 0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}};
                ++buffer_ptr;
                *buffer_ptr = DrawVertex{{+0.0f, -0.5f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f}};
                ++buffer_ptr;
                cmd_list.copy_buffer_to_buffer({
                    .src_buffer = vertex_staging_buffer,
                    .dst_buffer = vertex_buffer,
                    .size = sizeof(DrawVertex) * 3,
                });
            },
            .debug_name = APPNAME_PREFIX("Upload vertices"),
        });
        new_task_list.add_task({
            .used_buffers = {
                {task_vertex_buffer, daxa::TaskBufferAccess::VERTEX_SHADER_READ_ONLY},
            },
            .used_images = {
                {task_swapchain_image, daxa::TaskImageAccess::COLOR_ATTACHMENT, daxa::ImageMipArraySlice{}},
                {task_depth_image, daxa::TaskImageAccess::DEPTH_ATTACHMENT, daxa::ImageMipArraySlice{}},
            },
            .task = [this](daxa::TaskRuntime runtime) {
                auto cmd_list = runtime.get_command_list();
                cmd_list.begin_renderpass({
                    .color_attachments = {{
                        .image_view = swapchain_image.default_view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = std::array<f32, 4>{0.2f, 0.4f, 1.0f, 1.0f},
                    }},
                    .depth_attachment = {{
                        .image_view = depth_image.default_view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = daxa::DepthValue{1.0f, 0},
                    }},
                    .render_area = {.x = 0, .y = 0, .width = size_x, .height = size_y},
                });
                cmd_list.set_pipeline(draw_raster_pipeline);

                // cmd_list.push_constant(DrawPush{
                //     .gpu_input = this->device.get_device_address(gpu_input_buffer),
                //     .vertices = this->device.get_device_address(vertex_buffer),
                // });
                // cmd_list.draw({.vertex_count = 3});

                halflife.render(cmd_list, gpu_input_buffer);

                cmd_list.end_renderpass();
            },
            .debug_name = APPNAME_PREFIX("Draw to swapchain"),
        });
    }
};

int main() {
    App app = {};
    while (true) {
        if (app.update())
            break;
    }
}
