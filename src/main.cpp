#include "utils/base_app.hpp"

#include "utils/player.hpp"

#include <span>
#include <set>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include "common.hpp"
#include "wad.hpp"
#include "bsp.hpp"
#include "ConfigXML.hpp"

#include <imgui_stdlib.h>
#include <ImGuizmo.h>

#if COUNT_DRAWS
usize draw_count = 0;
#endif

#define SHOW_IMAGES_GUI 0

const std::string config_name = "halflife";

struct HalfLife {
    ConfigXML *xmlconfig = new ConfigXML();
    std::vector<BSP *> maps;
    daxa::Device &device;

    daxa::SamplerId lmap_image_sampler;
    daxa::SamplerId tex_image_samplers[4];

    i32 tex_image_sampler_i = 3;

    HalfLife(daxa::Device &a_device) : device{a_device} {
        xmlconfig->LoadProgramConfig();
        auto map_config_file = config_name + ".xml";
        xmlconfig->LoadMapConfig(map_config_file.c_str());

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

        lmap_image_sampler = device.create_sampler({
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .min_lod = 0,
            .max_lod = 0,
            .name = "lmap_image_sampler",
        });

        tex_image_samplers[0] = device.create_sampler({
            .magnification_filter = daxa::Filter::NEAREST,
            .minification_filter = daxa::Filter::NEAREST,
            .address_mode_u = daxa::SamplerAddressMode::REPEAT,
            .address_mode_v = daxa::SamplerAddressMode::REPEAT,
            .address_mode_w = daxa::SamplerAddressMode::REPEAT,
            .name = "tex_image_samplers[0]",
        });
        tex_image_samplers[1] = device.create_sampler({
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::REPEAT,
            .address_mode_v = daxa::SamplerAddressMode::REPEAT,
            .address_mode_w = daxa::SamplerAddressMode::REPEAT,
            .name = "tex_image_samplers[1]",
        });
        tex_image_samplers[2] = device.create_sampler({
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::REPEAT,
            .address_mode_v = daxa::SamplerAddressMode::REPEAT,
            .address_mode_w = daxa::SamplerAddressMode::REPEAT,
            .min_lod = 0,
            .max_lod = 3,
            .name = "tex_image_samplers[2]",
        });
        tex_image_samplers[3] = device.create_sampler({
            .magnification_filter = daxa::Filter::LINEAR,
            .minification_filter = daxa::Filter::LINEAR,
            .address_mode_u = daxa::SamplerAddressMode::REPEAT,
            .address_mode_v = daxa::SamplerAddressMode::REPEAT,
            .address_mode_w = daxa::SamplerAddressMode::REPEAT,
            .enable_anisotropy = true,
            .max_anisotropy = 16.0f,
            .min_lod = 0,
            .max_lod = 3,
            .name = "tex_image_samplers[3]",
        });

        {
            daxa::TaskGraph mip_task_graph = daxa::TaskGraph({
                .device = device,
                .name = "mip task list",
            });
            std::set<daxa::ImageId> image_set;
            for (auto map : maps) {
                for (auto [key, tex] : map->texturedTris) {
                    image_set.emplace(tex.image_id);
                }
            }
            std::vector<daxa::ImageId> images = {};
            std::vector<daxa::ImageSliceState> latest_slice_states = {};
            auto image_n = image_set.size();
            images.reserve(image_n);
            latest_slice_states.reserve(image_n);
            for (auto image_id : image_set) {
                if (image_id.version == 0)
                    continue;
                images.push_back(image_id);
                latest_slice_states.push_back({.latest_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL});
            }
            image_n = images.size();
            auto task_mip_image = daxa::TaskImage({.initial_images = {images, latest_slice_states}, .name = "task_mip_image"});
            mip_task_graph.use_persistent_image(task_mip_image);
            for (u32 i = 0; i < 3; ++i) {
                auto mip_view_a = task_mip_image.view().view({.base_mip_level = i});
                auto mip_view_b = task_mip_image.view().view({.base_mip_level = i + 1});
                mip_task_graph.add_task({
                    .uses = {
                        daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_READ>{mip_view_a},
                        daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_WRITE>{mip_view_b},
                    },
                    .task = [=, this](daxa::TaskInterface const &runtime) {
                        auto cmd_list = runtime.get_command_list();
                        for (size_t j = 0; j < image_n; ++j) {
                            auto image_id = runtime.uses[mip_view_a].image(j);
                            auto image_info = device.info_image(image_id);
                            auto mip_size = std::array<i32, 3>{std::max<i32>(1, static_cast<i32>(image_info.size.x)), std::max<i32>(1, static_cast<i32>(image_info.size.y)), std::max<i32>(1, static_cast<i32>(image_info.size.z))};
                            for (u32 k = 0; k < i; ++k) {
                                mip_size = {std::max<i32>(1, mip_size[0] / 2), std::max<i32>(1, mip_size[1] / 2), std::max<i32>(1, mip_size[2] / 2)};
                            }
                            auto next_mip_size = std::array<i32, 3>{std::max<i32>(1, mip_size[0] / 2), std::max<i32>(1, mip_size[1] / 2), std::max<i32>(1, mip_size[2] / 2)};
                            cmd_list.blit_image_to_image({
                                .src_image = image_id,
                                .src_image_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL, // TODO: get from TaskInterface
                                .dst_image = image_id,
                                .dst_image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                                .src_slice = {
                                    .mip_level = i,
                                    .base_array_layer = 0,
                                    .layer_count = 1,
                                },
                                .src_offsets = {{{0, 0, 0}, {mip_size[0], mip_size[1], mip_size[2]}}},
                                .dst_slice = {
                                    .mip_level = i + 1,
                                    .base_array_layer = 0,
                                    .layer_count = 1,
                                },
                                .dst_offsets = {{{0, 0, 0}, {next_mip_size[0], next_mip_size[1], next_mip_size[2]}}},
                                .filter = daxa::Filter::LINEAR,
                            });
                        }
                    },
                    .name = "mip_level_" + std::to_string(i),
                });
            }
            mip_task_graph.add_task({
                .uses = {
                    daxa::TaskImageUse<daxa::TaskImageAccess::FRAGMENT_SHADER_SAMPLED>{task_mip_image.view().view({.base_mip_level = 0, .level_count = 4})},
                },
                .task = [](daxa::TaskInterface const &) {},
                .name = "Transition",
            });
            mip_task_graph.submit({});
            mip_task_graph.complete({});
            mip_task_graph.execute({});
            device.wait_idle();
        }
    }

    ~HalfLife() {
        for (auto &map : maps) {
#if EXPORT_ASSETS
            map->export_mesh();
#endif
            device.destroy_image(map->lmap_image_id);
            for (usize i = 0; i < map->texturedTris.size(); ++i) {
                auto &buf = map->bufObjects[i];
                device.destroy_buffer(buf.buffer_id);
            }
        }
        for (auto &[key, tex] : textures) {
            if (tex.image_id.version != 0)
                device.destroy_image(tex.image_id);
        }
        device.destroy_sampler(lmap_image_sampler);
        for (auto sampler : tex_image_samplers)
            device.destroy_sampler(sampler);
    }

    void render(daxa::CommandList &cmd_list, daxa::BufferId gpu_input_buffer) {
        for (auto &map : maps) {
            map->render(device, cmd_list, gpu_input_buffer, tex_image_samplers[tex_image_sampler_i], lmap_image_sampler);
        }
    }
};

constexpr usize VERTEX_N = 6;

struct App : BaseApp<App> {
    // clang-format off
    std::shared_ptr<daxa::RasterPipeline> draw_raster_pipeline = pipeline_manager.add_raster_pipeline({
        .vertex_shader_info = daxa::ShaderCompileInfo{.source = daxa::ShaderFile{"draw.glsl"}, .compile_options = {.defines = {daxa::ShaderDefine{"DRAW_VERT"}}}},
        .fragment_shader_info = daxa::ShaderCompileInfo{.source = daxa::ShaderFile{"draw.glsl"}, .compile_options = {.defines = {daxa::ShaderDefine{"DRAW_FRAG"}}}},
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
        .name = APPNAME_PREFIX("draw_raster_pipeline"),
    }).value();
    // clang-format on
    daxa::BufferId gpu_input_buffer = device.create_buffer(daxa::BufferInfo{
        .size = sizeof(GpuInput),
        .name = APPNAME_PREFIX("gpu_input_buffer"),
    });

    daxa::TaskBuffer task_vertex_buffer;
    std::vector<daxa::BufferId> vertex_buffers;

    f32 render_scl = 1.0f;
    u32vec2 render_size = calc_render_size();

    auto calc_render_size() -> u32vec2 {
        return {
            static_cast<u32>(size_x * render_scl),
            static_cast<u32>(size_y * render_scl),
        };
    }

    daxa::ImageId color_image = device.create_image({
        .format = daxa::Format::R8G8B8A8_SRGB,
        .size = {render_size.x, render_size.y, 1},
        .usage = daxa::ImageUsageFlagBits::COLOR_ATTACHMENT | daxa::ImageUsageFlagBits::TRANSFER_SRC,
    });
    daxa::TaskImage task_color_image;
    daxa::ImageId depth_image = device.create_image({
        .format = daxa::Format::D24_UNORM_S8_UINT,
        .size = {render_size.x, render_size.y, 1},
        .usage = daxa::ImageUsageFlagBits::DEPTH_STENCIL_ATTACHMENT,
    });
    daxa::TaskImage task_depth_image;

    std::filesystem::path data_directory = ".";

    std::vector<nlohmann::json> saved_settings;

    auto get_settings_json() -> nlohmann::json {
        auto json = nlohmann::json{};
        for (auto &map : halflife.maps) {
            json[map->mapId] = nlohmann::json{map->user_offset.x, map->user_offset.y, map->user_offset.z};
        }
        return json;
    }
    void set_settings_json(nlohmann::json &json) {
        for (auto &map : halflife.maps) {
            if (json.contains(map->mapId)) {
                auto &v = json[map->mapId];
                map->user_offset = {v[0], v[1], v[2]};
            }
        }
    }
    auto save_settings_file(nlohmann::json &json) {
        auto f = std::ofstream(data_directory / (config_name + "-offsets.json"));
        f << std::setw(4) << json;
    }

    auto push_settings() {
        auto json = get_settings_json();
        saved_settings.push_back(json);
        save_settings_file(json);
        // std::cout << "-> " << saved_settings.size() << std::endl;
    }
    auto pop_settings() {
        // std::cout << "<- " << saved_settings.size() << std::endl;
        if (saved_settings.size() <= 1)
            return;
        saved_settings.pop_back();
        auto new_json = saved_settings.back();
        set_settings_json(new_json);
        save_settings_file(new_json);
    }

    void save_settings() {
        auto json = get_settings_json();
        save_settings_file(json);
    }
    void load_settings() {
        auto settings_file = data_directory / (config_name + "-offsets.json");
        if (!std::filesystem::exists(settings_file))
            return;
        auto json = nlohmann::json::parse(std::ifstream(settings_file));
        set_settings_json(json);
        saved_settings.push_back(json);
    }

    GpuInput gpu_input = {};
    Player3D player = {
        .pos = {0.0f, 0.0f, -1.0f},
        .rot = {0.0f, 0.0f, 0.0f},
    };
    bool paused = true;
    bool is_using_gizmo = false;

    ImGuizmo::OPERATION current_gizmo_op = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE current_gizmo_mode = ImGuizmo::LOCAL;

    HalfLife halflife = HalfLife(device);

    daxa::TaskGraph loop_task_graph = record_loop_task_graph();

    App() {
        load_settings();

        player.camera.resize(static_cast<i32>(size_x), static_cast<i32>(size_y));
        player.camera.set_pos(player.pos);
        player.camera.set_rot(player.rot.x, player.rot.y);
        player.update(1.0f);
    }
    ~App() {
        device.wait_idle();
        device.collect_garbage();
        device.destroy_buffer(gpu_input_buffer);
        device.destroy_image(depth_image);
        device.destroy_image(color_image);
    }

    void gizmo(BSP *map) {
        auto cam_view = glm::translate(glm::rotate(glm::rotate(glm::mat4(1), -player.rot.y, {1, 0, 0}), player.rot.x, {0, 1, 0}), glm::vec3(player.pos.x, -player.pos.y, player.pos.z));
        auto cam_proj = player.camera.proj_mat;
        auto base_offset = glm::vec3{
            -(map->offset.x + map->ConfigOffsetChapter.x + map->propagated_user_offset.x),
            map->offset.y + map->ConfigOffsetChapter.y + map->propagated_user_offset.y,
            -(map->offset.z + map->ConfigOffsetChapter.z + map->propagated_user_offset.z),
        };
        auto modl_mat = glm::translate(glm::mat4(1.0f), base_offset);
        ImGuiIO &io = ImGui::GetIO();
        ImGuizmo::SetID(static_cast<int>(reinterpret_cast<usize>(map)));
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
        f32 snap = 1.0f;
        ImGuizmo::Manipulate(&cam_view[0][0], &cam_proj[0][0], current_gizmo_op, current_gizmo_mode, &modl_mat[0][0], nullptr, &snap, nullptr, nullptr);
        f32vec3 modl_trn, modl_rot, modl_scl;
        ImGuizmo::DecomposeMatrixToComponents(&modl_mat[0][0], &modl_trn[0], &modl_rot[0], &modl_scl[0]);
        map->user_offset = map->user_offset + f32vec3{-(modl_trn.x - base_offset.x), modl_trn.y - base_offset.y, -(modl_trn.z - base_offset.z)};
    }
    void ui_update() {
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        auto was_using_gizmo = is_using_gizmo;
        is_using_gizmo = ImGuizmo::IsUsing();

        if (was_using_gizmo && !is_using_gizmo) {
            // performed action!
            push_settings();
        }

        if (paused) {
            // ImGui::ShowDemoWindow();
            ImGui::ShowMetricsWindow();

            ImGui::Begin("Debug");

            if (ImGui::SliderFloat("Render Scale", &render_scl, 0.1f, 2.0f)) {
                recreate_render_image(color_image, task_color_image);
                recreate_render_image(depth_image, task_depth_image);
            }

            ImGui::SliderInt("Sampler", &halflife.tex_image_sampler_i, 0, 3);

            ImGui::SliderFloat("Move Speed", &player.speed, 50.0f, 400.0f);
            ImGui::SliderFloat("Sprint Multiplier", &player.sprint_speed, 1.0f, 50.0f);

            for (auto map : halflife.maps) {
                ImGui::PushID(static_cast<int>(reinterpret_cast<usize>(map)));
                ImGui::Checkbox(map->mapId.c_str(), &map->should_draw);
                ImGui::PopID();

                auto parent_iter = std::find_if(halflife.maps.begin(), halflife.maps.end(), [map](auto const &m) { return map->parent_mapId == m->mapId; });
                if (parent_iter != halflife.maps.end()) {
                    map->propagated_user_offset = map->user_offset + (*parent_iter)->propagated_user_offset;
                } else {
                    map->propagated_user_offset = map->user_offset;
                }

                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(reinterpret_cast<usize>(map)) + 1);
                ImGui::Checkbox("[Gizmo]", &map->show_gizmo);
                if (map->show_gizmo)
                    gizmo(map);
                ImGui::PopID();

                if (map->should_draw) {
                    auto offset_str = map->mapId + " offset";
                    auto parent_str = map->mapId + " parent";
                    ImGui::SameLine();
                    if (ImGui::InputFloat3(offset_str.c_str(), &map->user_offset.x))
                        save_settings();
                    ImGui::SameLine();
                    ImGui::InputText(parent_str.c_str(), &map->parent_mapId);
                }
            }

#if COUNT_DRAWS
            ImGui::Text("Draw Count: %llu", draw_count);
            draw_count = 0;
#endif

            ImGui::End();

#if SHOW_IMAGES_GUI
            // ImGui::Begin("Images");
            // u32 i = 0;
            // for (auto &[key, tex] : textures) {
            //     if ((i % 12) != 0)
            //         ImGui::SameLine();
            //     ++i;
            //     ImGui::Image(*reinterpret_cast<ImTextureID const *>(&tex.image_id), ImVec2(static_cast<f32>(tex.w), static_cast<f32>(tex.h)));
            // }
            // for (auto &map : halflife.maps) {
            //     ImGui::Image(*reinterpret_cast<ImTextureID const *>(&map->lmap_image_id), {1024, 1024});
            // }
            // ImGui::End();
#endif
        }
        ImGui::Render();
    }
    void on_update() {
        pipeline_manager.reload_all();
        ui_update();

        player.camera.resize(static_cast<i32>(size_x), static_cast<i32>(size_y));
        player.camera.set_pos(player.pos);
        player.camera.set_rot(player.rot.x, player.rot.y);
        player.update(delta_time);

        swapchain_image = swapchain.acquire_next_image();
        task_swapchain_image.set_images({.images = {&swapchain_image, 1}});
        if (swapchain_image.is_empty())
            return;

        auto mat = player.camera.get_vp();
        gpu_input.mvp_mat = daxa::math_operators::mat_from_span<f32, 4, 4>(std::span<f32, 4 * 4>{glm::value_ptr(mat), 4 * 4});

        auto buffer_n = size_t{};
        for (auto &map : halflife.maps) {
            buffer_n += map->bufObjects.size();
        }
        if (buffer_n == 0)
            return;

        vertex_buffers.resize(buffer_n);
        auto buffer_i = size_t{};
        for (auto &map : halflife.maps) {
            for (auto &buf : map->bufObjects) {
                vertex_buffers[buffer_i] = buf.buffer_id;
                ++buffer_i;
            }
        }
        task_vertex_buffer.set_buffers({.buffers = vertex_buffers});

        loop_task_graph.execute({});

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
        if (key_id == GLFW_KEY_U && action == GLFW_PRESS)
            pop_settings();
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
            recreate_render_image(color_image, task_color_image);
            recreate_render_image(depth_image, task_depth_image);
            on_update();
        }
    }
    void recreate_render_image(daxa::ImageId &image_id, daxa::TaskImage &task_image_id) {
        auto image_info = device.info_image(image_id);
        device.destroy_image(image_id);
        render_size = calc_render_size();
        image_info.size = {render_size.x, render_size.y, 1},
        image_id = device.create_image(image_info);
        task_image_id.set_images({.images = {&image_id, 1}});
    }

    void toggle_pause() {
        set_mouse_capture(paused);
        paused = !paused;
    }
    void record_tasks(daxa::TaskGraph &new_task_graph) {
        task_color_image = daxa::TaskImage({.initial_images = {.images = {&color_image, 1}}, .name = APPNAME_PREFIX("task_color_image")});
        new_task_graph.use_persistent_image(task_color_image);
        task_depth_image = daxa::TaskImage({.initial_images = {.images = {&depth_image, 1}}, .name = APPNAME_PREFIX("task_depth_image")});
        new_task_graph.use_persistent_image(task_depth_image);

        task_vertex_buffer = daxa::TaskBuffer({.name = APPNAME_PREFIX("task_vertex_buffer")});
        new_task_graph.use_persistent_buffer(task_vertex_buffer);

        new_task_graph.add_task({
            .uses = {
                daxa::TaskBufferUse<daxa::TaskBufferAccess::TRANSFER_WRITE>{task_vertex_buffer},
            },
            .task = [this](daxa::TaskInterface runtime) {
                auto cmd_list = runtime.get_command_list();
                auto gpu_input_staging_buffer = device.create_buffer({
                    .size = sizeof(GpuInput),
                    .allocate_info = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
                    .name = APPNAME_PREFIX("gpu_input_staging_buffer"),
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
            .name = APPNAME_PREFIX("Upload input"),
        });
        new_task_graph.add_task({
            .uses = {
                daxa::TaskBufferUse<daxa::TaskBufferAccess::VERTEX_SHADER_READ>{task_vertex_buffer},
                daxa::TaskImageUse<daxa::TaskImageAccess::COLOR_ATTACHMENT>{task_color_image},
                daxa::TaskImageUse<daxa::TaskImageAccess::DEPTH_ATTACHMENT>{task_depth_image},
            },
            .task = [this](daxa::TaskInterface runtime) {
                auto cmd_list = runtime.get_command_list();
                cmd_list.begin_renderpass({
                    .color_attachments = {{
                        .image_view = color_image.default_view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = std::array<f32, 4>{51.0f / 255.0f, 102.0f / 255.0f, 250.0f / 255.0f, 1.0f},
                    }},
                    .depth_attachment = {{
                        .image_view = depth_image.default_view(),
                        .load_op = daxa::AttachmentLoadOp::CLEAR,
                        .clear_value = daxa::DepthValue{1.0f, 0},
                    }},
                    .render_area = {.x = 0, .y = 0, .width = render_size.x, .height = render_size.y},
                });
                cmd_list.set_pipeline(*draw_raster_pipeline);
                halflife.render(cmd_list, gpu_input_buffer);
                cmd_list.end_renderpass();
            },
            .name = APPNAME_PREFIX("Draw to render images"),
        });
        new_task_graph.add_task({
            .uses = {
                daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_READ>{task_color_image},
                daxa::TaskImageUse<daxa::TaskImageAccess::TRANSFER_WRITE>{task_swapchain_image},
            },
            .task = [this](daxa::TaskInterface task_runtime) {
                auto cmd_list = task_runtime.get_command_list();
                cmd_list.blit_image_to_image({
                    .src_image = color_image,
                    .src_image_layout = daxa::ImageLayout::TRANSFER_SRC_OPTIMAL,
                    .dst_image = swapchain_image,
                    .dst_image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
                    .src_offsets = {{{0, 0, 0}, {static_cast<i32>(render_size.x), static_cast<i32>(render_size.y), 1}}},
                    .dst_offsets = {{{0, 0, 0}, {static_cast<i32>(size_x), static_cast<i32>(size_y), 1}}},
                    .filter = daxa::Filter::LINEAR,
                });
            },
            .name = APPNAME_PREFIX("Blit (render to swapchain)"),
        });
    }
};

int main() {
    App app = {};
#if EXPORT_ASSETS
    app.update();
#else
    while (true) {
        if (app.update())
            break;
    }
#endif
}
