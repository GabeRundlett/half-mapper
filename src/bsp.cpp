#include "common.hpp"
#include "bsp.hpp"
#include "entities.hpp"
#include "ConfigXML.hpp"
#include <cstring>

#include <png.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/Exporter.hpp>

struct AssetExporter {
    aiScene scene;

    std::vector<aiMaterial *> materials;
    std::vector<aiMesh *> meshes;

    AssetExporter() {
        scene.mRootNode = new aiNode();
    }

    ~AssetExporter() {
        scene.mMaterials = materials.data();
        scene.mNumMaterials = materials.size();
        scene.mMeshes = meshes.data();
        scene.mNumMeshes = meshes.size();

        Assimp::Exporter exporter;
        const aiExportFormatDesc *format = exporter.GetExportFormatDescription(0);
        exporter.Export(&scene, format->id, "assets_out/halflife.dae");

        scene.mMaterials = nullptr;
        scene.mNumMaterials = 0;
        scene.mMeshes = nullptr;
        scene.mNumMeshes = 0;
    }
};

std::map<std::string, BSP_TEXTURE> textures;
std::map<std::string, std::vector<std::pair<VERTEX, std::string>>> landmarks;
std::map<std::string, std::vector<std::string>> dontRenderModel;
std::map<std::string, VERTEX> offsets;

static AssetExporter exporter;

// Correct UV coordinates
static inline auto calcCoords(VERTEX v, VERTEX vs, VERTEX vt, float sShift, float tShift) -> COORDS {
    COORDS ret{};
    ret.u = sShift + vs.x * v.x + vs.y * v.y + vs.z * v.z;
    ret.v = tShift + vt.x * v.x + vt.y * v.y + vt.z * v.z;
    return ret;
}

static int save_png(std::string filename, i32 width, i32 height, i32 bitdepth, i32 colortype, u8 *data, i32 pitch, i32 transform) {
#if EXPORT_IMAGES
    int i = 0;
    int r = 0;
    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep *row_pointers = NULL;
    if (NULL == data) {
        printf("Error: failed to save the png because the given data is NULL.\n");
        r = -1;
        goto error;
    }
    if (0 == filename.size()) {
        printf("Error: failed to save the png because the given filename length is 0.\n");
        r = -2;
        goto error;
    }
    if (0 == pitch) {
        printf("Error: failed to save the png because the given pitch is 0.\n");
        r = -3;
        goto error;
    }
    fp = fopen(filename.c_str(), "wb");
    if (NULL == fp) {
        printf("Error: failed to open the png file: %s\n", filename.c_str());
        r = -4;
        goto error;
    }
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (NULL == png_ptr) {
        printf("Error: failed to create the png write struct.\n");
        r = -5;
        goto error;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (NULL == info_ptr) {
        printf("Error: failed to create the png info struct.\n");
        r = -6;
        goto error;
    }
    png_set_IHDR(png_ptr,
                 info_ptr,
                 width,
                 height,
                 bitdepth,           /* e.g. 8 */
                 colortype,          /* PNG_COLOR_TYPE_{GRAY, PALETTE, RGB, RGB_ALPHA, GRAY_ALPHA, RGBA, GA} */
                 PNG_INTERLACE_NONE, /* PNG_INTERLACE_{NONE, ADAM7 } */
                 PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);
    row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    for (i = 0; i < height; ++i) {
        row_pointers[i] = data + i * pitch;
    }
    png_init_io(png_ptr, fp);
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr, info_ptr, transform, NULL);
error:
    if (NULL != fp) {
        fclose(fp);
        fp = NULL;
    }
    if (NULL != png_ptr) {
        if (NULL == info_ptr) {
            printf("Error: info ptr is null. not supposed to happen here.\n");
        }
        png_destroy_write_struct(&png_ptr, &info_ptr);
        png_ptr = NULL;
        info_ptr = NULL;
    }
    if (NULL != row_pointers) {
        free(row_pointers);
        row_pointers = NULL;
    }
    return r;
#else
    return 0;
#endif
}

void BSP::export_mesh() {
#if EXPORT_MESHES
    auto scene_node = new aiNode();

    auto const mesh_n = this->texturedTris.size();

    auto const offset_i = exporter.materials.size();
    exporter.materials.reserve(mesh_n + offset_i);
    exporter.meshes.reserve(mesh_n + offset_i);

    scene_node->mMeshes = new u32[mesh_n];
    scene_node->mNumMeshes = mesh_n;
    scene_node->mName = mapId;

    usize mesh_i = 0;
    for (auto [texture_name, texture_mesh_info] : this->texturedTris) {
        exporter.materials.push_back(new aiMaterial());
        exporter.meshes.push_back(new aiMesh());

        scene_node->mMeshes[mesh_i] = mesh_i + offset_i;

        auto &mesh = *exporter.meshes.back();
        mesh.mMaterialIndex = mesh_i + offset_i;

        // Create material
        {
            auto *tex_str_ptr = new aiString(texture_name + ".png");
            auto &mat = *exporter.materials.back();
            mat.AddProperty(tex_str_ptr, AI_MATKEY_TEXTURE(u32(aiTextureType_DIFFUSE), 0u));
        }

        // generate mesh
        {
            usize vert_n = texture_mesh_info.triangles.size();
            mesh.mVertices = new aiVector3D[vert_n];
            mesh.mNumVertices = vert_n;
            mesh.mTextureCoords[0] = new aiVector3D[vert_n];
            mesh.mNumUVComponents[0] = vert_n;
            usize vert_i = 0;
            for (auto const &v : texture_mesh_info.triangles) {
                f32vec3 full_offset{offset.x + ConfigOffsetChapter.x, offset.y + ConfigOffsetChapter.y, offset.z + ConfigOffsetChapter.z};
                auto o = full_offset + propagated_user_offset;
                mesh.mVertices[vert_i] = aiVector3D(v.x + o.x, v.y + o.y, v.z + o.z) * 0.0254f;
                mesh.mTextureCoords[0][vert_i] = aiVector3D(v.u, 1.0f - v.v, 0);
                ++vert_i;
            }
            usize face_n = vert_n / 3;
            mesh.mFaces = new aiFace[face_n];
            mesh.mNumFaces = face_n;
            for (usize face_i = 0; face_i < face_n; ++face_i) {
                aiFace &face = mesh.mFaces[face_i];
                face.mIndices = new u32[3];
                face.mNumIndices = 3;
                face.mIndices[0] = face_i * 3 + 0;
                face.mIndices[1] = face_i * 3 + 1;
                face.mIndices[2] = face_i * 3 + 2;
            }
        }
        ++mesh_i;
    }
    exporter.scene.mRootNode->addChildren(1, &scene_node);
#endif
}

void BSP_TEXTURE::load(daxa::Device &device, std::string const &tex_name, u8 *data, u32 src_channel_n, u32 dst_channel_n, u32 mip_level_count) {
#if EXPORT_ASSETS
    png_byte color_type = PNG_COLOR_TYPE_RGBA;
    if (src_channel_n == 3)
        color_type = PNG_COLOR_TYPE_RGB;
    save_png("assets_out/" + tex_name + ".png", w, h, 8, color_type, data, src_channel_n * w, PNG_TRANSFORM_IDENTITY);
#endif

    auto sx = static_cast<u32>(w);
    auto sy = static_cast<u32>(h);
    usize image_size = sx * sy * sizeof(u8) * dst_channel_n;
    auto texture_staging_buffer = device.create_buffer({
        .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        .size = static_cast<u32>(image_size),
        .debug_name = "texture_staging_buffer",
    });
    u8 *staging_buffer_ptr = device.get_host_address_as<u8>(texture_staging_buffer);
    for (usize i = 0; i < sx * sy; ++i) {
        usize src_offset = i * src_channel_n;
        usize dst_offset = i * dst_channel_n;
        for (usize ci = 0; ci < std::min(src_channel_n, dst_channel_n); ++ci) {
            staging_buffer_ptr[ci + dst_offset] = data[ci + src_offset];
        }
    }
    auto cmd_list = device.create_command_list({
        .debug_name = "cmd_list",
    });
    cmd_list.pipeline_barrier({
        .awaited_pipeline_access = daxa::AccessConsts::HOST_WRITE,
        .waiting_pipeline_access = daxa::AccessConsts::TRANSFER_READ,
    });
    cmd_list.pipeline_barrier_image_transition({
        .awaited_pipeline_access = daxa::AccessConsts::HOST_WRITE,
        .waiting_pipeline_access = daxa::AccessConsts::TRANSFER_WRITE,
        .before_layout = daxa::ImageLayout::UNDEFINED,
        .after_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
        .image_slice = {
            .base_mip_level = 0,
            .level_count = mip_level_count,
            .base_array_layer = 0,
            .layer_count = 1,
        },
        .image_id = image_id,
    });
    cmd_list.copy_buffer_to_image({
        .buffer = texture_staging_buffer,
        .image = image_id,
        .image_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
        .image_offset = {0, 0, 0},
        .image_extent = {sx, sy, 1},
    });
    // cmd_list.pipeline_barrier_image_transition({
    //     .awaited_pipeline_access = daxa::AccessConsts::TRANSFER_WRITE,
    //     .waiting_pipeline_access = daxa::AccessConsts::READ,
    //     .before_layout = daxa::ImageLayout::TRANSFER_DST_OPTIMAL,
    //     .after_layout = daxa::ImageLayout::READ_ONLY_OPTIMAL,
    //     .image_slice = {
    //         .base_mip_level = 0,
    //         .level_count = 1,
    //         .base_array_layer = 0,
    //         .layer_count = 1,
    //     },
    //     .image_id = image_id,
    // });
    cmd_list.complete();
    device.submit_commands({
        .command_lists = {std::move(cmd_list)},
    });
    device.wait_idle();
    device.destroy_buffer(texture_staging_buffer);
}

void upload_buffer_data(daxa::Device &device, daxa::BufferId buffer_id, u8 *data, u32 size) {
    auto staging_buffer = device.create_buffer({
        .memory_flags = daxa::MemoryFlagBits::HOST_ACCESS_RANDOM,
        .size = static_cast<u32>(size),
        .debug_name = "staging_buffer",
    });
    u8 *staging_buffer_ptr = device.get_host_address_as<u8>(staging_buffer);
    std::copy(data, data + size, staging_buffer_ptr);
    auto cmd_list = device.create_command_list({
        .debug_name = "cmd_list",
    });
    cmd_list.pipeline_barrier({
        .awaited_pipeline_access = daxa::AccessConsts::HOST_WRITE,
        .waiting_pipeline_access = daxa::AccessConsts::TRANSFER_WRITE,
    });
    cmd_list.copy_buffer_to_buffer({
        .src_buffer = staging_buffer,
        .dst_buffer = buffer_id,
        .size = size,
    });
    cmd_list.pipeline_barrier({
        .awaited_pipeline_access = daxa::AccessConsts::TRANSFER_WRITE,
        .waiting_pipeline_access = daxa::AccessConsts::READ,
    });
    cmd_list.complete();
    device.submit_commands({
        .command_lists = {std::move(cmd_list)},
    });
    device.wait_idle();
    device.destroy_buffer(staging_buffer);
}

BSP::BSP(daxa::Device &device, const std::vector<std::string> &szGamePaths, const std::string &filename, const MapEntry &sMapEntry) {
    std::string const id = sMapEntry.m_szName;

    uint8_t gammaTable[256];
    for (int i = 0; i < 256; i++) {
        gammaTable[i] = pow(i / 255.0, 1.0 / 3.0) * 255;
    }

    // Light map atlas
    lmapAtlas = new uint8_t[1024 * 1024 * 3];

    std::ifstream inBSP;

    // Try to open the file from all known gamepaths.
    for (const auto &szGamePath : szGamePaths) {
        if (!inBSP.is_open()) {
            inBSP.open(szGamePath + filename, std::ios::binary);
        }
    }

    // If the BSP wasn't found in any of the gamepaths...
    if (!inBSP.is_open()) {
        std::cerr << "Can't open BSP " << filename << "." << std::endl;
        return;
    }

    // Check BSP version
    BSPHEADER bHeader{};
    inBSP.read((char *)&bHeader, sizeof(bHeader));
    if (bHeader.nVersion != 30) {
        std::cerr << "BSP version is not 30 (" << filename << ")." << std::endl;
        return;
    }

    // Read Entities
    inBSP.seekg(bHeader.lump[LUMP_ENTITIES].nOffset, std::ios::beg);
    char *bff = new char[bHeader.lump[LUMP_ENTITIES].nLength];
    inBSP.read(bff, bHeader.lump[LUMP_ENTITIES].nLength);
    parse_entities(bff, id, sMapEntry);
    delete[] bff;

    // Read Models and hide some faces
    auto *models = new BSPMODEL[bHeader.lump[LUMP_MODELS].nLength / (int)sizeof(BSPMODEL)];
    inBSP.seekg(bHeader.lump[LUMP_MODELS].nOffset, std::ios::beg);
    inBSP.read((char *)models, bHeader.lump[LUMP_MODELS].nLength);

    std::map<int, bool> dontRenderFace;
    for (auto &i : dontRenderModel[id]) {
        int const modelId = atoi(i.substr(1).c_str());
        int const startingFace = models[modelId].iFirstFace;
        for (int j = 0; j < models[modelId].nFaces; j++) {
            // if(modelId == 57) std::cout << j+startingFace << std::endl;
            dontRenderFace[j + startingFace] = true;
        }
    }

    // Read Vertices
    std::vector<VERTEX> vertices;
    inBSP.seekg(bHeader.lump[LUMP_VERTICES].nOffset, std::ios::beg);
    for (int i = 0; i < bHeader.lump[LUMP_VERTICES].nLength / 12; i++) {
        VERTEX v;
        inBSP.read((char *)&v, sizeof(v));
        vertices.push_back(v);
    }

    // Read Edges
    auto *edges = new BSPEDGE[bHeader.lump[LUMP_EDGES].nLength / (int)sizeof(BSPEDGE)];
    inBSP.seekg(bHeader.lump[LUMP_EDGES].nOffset, std::ios::beg);
    inBSP.read((char *)edges, bHeader.lump[LUMP_EDGES].nLength);

    // Read Surfedges
    std::vector<VERTEX> verticesPrime;
    inBSP.seekg(bHeader.lump[LUMP_SURFEDGES].nOffset, std::ios::beg);
    for (int i = 0; i < bHeader.lump[LUMP_SURFEDGES].nLength / (int)sizeof(int); i++) {
        int e = 0;
        inBSP.read((char *)&e, sizeof(e));
        verticesPrime.push_back(vertices[edges[e > 0 ? e : -e].iVertex[e > 0 ? 0 : 1]]);
    }

    // Read Lightmaps
    inBSP.seekg(bHeader.lump[LUMP_LIGHTING].nOffset, std::ios::beg);
    int const size = bHeader.lump[LUMP_LIGHTING].nLength;
    auto *lmap = new uint8_t[size];
    inBSP.read((char *)lmap, size);
    std::vector<LMAP> lmaps;

    // Read Textures
    inBSP.seekg(bHeader.lump[LUMP_TEXTURES].nOffset, std::ios::beg);
    BSPTEXTUREHEADER theader{};
    inBSP.read((char *)&theader, sizeof(theader));
    int *texOffSets = new int[theader.nMipTextures];
    inBSP.read((char *)texOffSets, theader.nMipTextures * sizeof(int));

    std::vector<std::string> texNames;

    for (u32 i = 0; i < theader.nMipTextures; i++) {
        inBSP.seekg(bHeader.lump[LUMP_TEXTURES].nOffset + texOffSets[i], std::ios::beg);

        BSPMIPTEX bmt{};
        inBSP.read((char *)&bmt, sizeof(bmt));
        if (!textures.contains(bmt.szName)) { // First appearance of the texture
            if (bmt.nOffsets[0] != 0 && bmt.nOffsets[1] != 0 && bmt.nOffsets[2] != 0 && bmt.nOffsets[3] != 0) {
                // Textures that are inside the BSP

                // Awful code. This and wad.cpp may be joined, they are pretty similar (except that these don't have color palettes)

                auto *data0 = new unsigned char[bmt.nWidth * bmt.nHeight];
                inBSP.seekg(bHeader.lump[LUMP_TEXTURES].nOffset + texOffSets[i] + bmt.nOffsets[0], std::ios::beg);
                inBSP.read((char *)data0, bmt.nWidth * bmt.nHeight);

                auto *data1 = new unsigned char[bmt.nWidth * bmt.nHeight / 4];
                inBSP.seekg(bHeader.lump[LUMP_TEXTURES].nOffset + texOffSets[i] + bmt.nOffsets[1], std::ios::beg);
                inBSP.read((char *)data1, bmt.nWidth * bmt.nHeight / 4);

                auto *data2 = new unsigned char[bmt.nWidth * bmt.nHeight / 16];
                inBSP.seekg(bHeader.lump[LUMP_TEXTURES].nOffset + texOffSets[i] + bmt.nOffsets[2], std::ios::beg);
                inBSP.read((char *)data2, bmt.nWidth * bmt.nHeight / 16);

                auto *data3 = new unsigned char[bmt.nWidth * bmt.nHeight / 64];
                inBSP.seekg(bHeader.lump[LUMP_TEXTURES].nOffset + texOffSets[i] + bmt.nOffsets[3], std::ios::beg);
                inBSP.read((char *)data3, bmt.nWidth * bmt.nHeight / 64);

                short dummy = 0;
                inBSP.read((char *)&dummy, 2);

                auto *data4 = new unsigned char[256 * 3];
                inBSP.read((char *)data4, 256 * 3);

                auto *dataFinal0 = new unsigned char[bmt.nWidth * bmt.nHeight * 4];
                auto *dataFinal1 = new unsigned char[bmt.nWidth * bmt.nHeight];
                auto *dataFinal2 = new unsigned char[bmt.nWidth * bmt.nHeight / 4];
                auto *dataFinal3 = new unsigned char[bmt.nWidth * bmt.nHeight / 16];

                for (u32 y = 0; y < bmt.nHeight; y++) {
                    for (u32 x = 0; x < bmt.nWidth; x++) {
                        dataFinal0[(x + y * bmt.nWidth) * 4] = data4[data0[y * bmt.nWidth + x] * 3];
                        dataFinal0[(x + y * bmt.nWidth) * 4 + 1] = data4[data0[y * bmt.nWidth + x] * 3 + 1];
                        dataFinal0[(x + y * bmt.nWidth) * 4 + 2] = data4[data0[y * bmt.nWidth + x] * 3 + 2];

                        if (dataFinal0[(x + y * bmt.nWidth) * 4] == 0 && dataFinal0[(x + y * bmt.nWidth) * 4 + 1] == 0 && dataFinal0[(x + y * bmt.nWidth) * 4 + 2] == 255) {
                            dataFinal0[(x + y * bmt.nWidth) * 4 + 3] = dataFinal0[(x + y * bmt.nWidth) * 4 + 2] = dataFinal0[(x + y * bmt.nWidth) * 4 + 1] = dataFinal0[(x + y * bmt.nWidth) * 4 + 0] = 0;
                        } else {
                            dataFinal0[(x + y * bmt.nWidth) * 4 + 3] = 255;
                        }
                    }
                }
                for (u32 y = 0; y < bmt.nHeight / 2; y++) {
                    for (u32 x = 0; x < bmt.nWidth / 2; x++) {
                        dataFinal1[(x + y * bmt.nWidth / 2) * 4] = data4[data1[y * bmt.nWidth / 2 + x] * 3];
                        dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 1] = data4[data1[y * bmt.nWidth / 2 + x] * 3 + 1];
                        dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 2] = data4[data1[y * bmt.nWidth / 2 + x] * 3 + 2];

                        if (dataFinal1[(x + y * bmt.nWidth / 2) * 4] == 0 && dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 1] == 0 && dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 2] == 255) {
                            dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 3] = dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 2] = dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 1] = dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 0] = 0;
                        } else {
                            dataFinal1[(x + y * bmt.nWidth / 2) * 4 + 3] = 255;
                        }
                    }
                }
                for (u32 y = 0; y < bmt.nHeight / 4; y++) {
                    for (u32 x = 0; x < bmt.nWidth / 4; x++) {
                        dataFinal2[(x + y * bmt.nWidth / 4) * 4] = data4[data2[y * bmt.nWidth / 4 + x] * 3];
                        dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 1] = data4[data2[y * bmt.nWidth / 4 + x] * 3 + 1];
                        dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 2] = data4[data2[y * bmt.nWidth / 4 + x] * 3 + 2];

                        if (dataFinal2[(x + y * bmt.nWidth / 4) * 4] == 0 && dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 1] == 0 && dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 2] == 255) {
                            dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 3] = dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 2] = dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 1] = dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 0] = 0;
                        } else {
                            dataFinal2[(x + y * bmt.nWidth / 4) * 4 + 3] = 255;
                        }
                    }
                }
                for (u32 y = 0; y < bmt.nHeight / 8; y++) {
                    for (u32 x = 0; x < bmt.nWidth / 8; x++) {
                        dataFinal3[(x + y * bmt.nWidth / 8) * 4] = data4[data3[y * bmt.nWidth / 8 + x] * 3];
                        dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 1] = data4[data3[y * bmt.nWidth / 8 + x] * 3 + 1];
                        dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 2] = data4[data3[y * bmt.nWidth / 8 + x] * 3 + 2];

                        if (dataFinal3[(x + y * bmt.nWidth / 8) * 4] == 0 && dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 1] == 0 && dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 2] == 255) {
                            dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 3] = dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 2] = dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 1] = dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 0] = 0;
                        } else {
                            dataFinal3[(x + y * bmt.nWidth / 8) * 4 + 3] = 255;
                        }
                    }
                }

                BSP_TEXTURE n{};
                n.w = bmt.nWidth;
                n.h = bmt.nHeight;

                n.image_id = device.create_image({
                    .format = daxa::Format::R8G8B8A8_SRGB,
                    .size = {bmt.nWidth, bmt.nHeight, 1},
                    .mip_level_count = 4,
                    .usage = daxa::ImageUsageFlagBits::SHADER_READ_ONLY | daxa::ImageUsageFlagBits::TRANSFER_SRC | daxa::ImageUsageFlagBits::TRANSFER_DST,
                    .debug_name = "image",
                });
                n.load(device, bmt.szName, dataFinal0);

                textures[bmt.szName] = n;

                delete[] data0;
                delete[] data1;
                delete[] data2;
                delete[] data3;
                delete[] data4;
                delete[] dataFinal0;
                delete[] dataFinal1;
                delete[] dataFinal2;
                delete[] dataFinal3;

            } else {
                BSP_TEXTURE n{};
                n.w = 1;
                n.h = 1;
                textures[bmt.szName] = n;
            }
        }
        texNames.emplace_back(bmt.szName);
    }

    // Read Texture information
    inBSP.seekg(bHeader.lump[LUMP_TEXINFO].nOffset, std::ios::beg);
    auto *btfs = new BSPTEXTUREINFO[bHeader.lump[LUMP_TEXINFO].nLength / (int)sizeof(BSPTEXTUREINFO)];
    inBSP.read((char *)btfs, bHeader.lump[LUMP_TEXINFO].nLength);

    // Read Faces and lightmaps
    inBSP.seekg(bHeader.lump[LUMP_FACES].nOffset, std::ios::beg);

    auto *minUV = new float[bHeader.lump[LUMP_FACES].nLength / (int)sizeof(BSPFACE) * 2];
    auto *maxUV = new float[bHeader.lump[LUMP_FACES].nLength / (int)sizeof(BSPFACE) * 2];

    for (int i = 0; i < bHeader.lump[LUMP_FACES].nLength / (int)sizeof(BSPFACE); i++) {
        BSPFACE f{};
        inBSP.read((char *)&f, sizeof(f));
        BSPTEXTUREINFO const b = btfs[f.iTextureInfo];
        std::string const faceTexName = texNames[b.iMiptex];

        minUV[i * 2] = minUV[i * 2 + 1] = 99999;
        maxUV[i * 2] = maxUV[i * 2 + 1] = -99999;

        for (int j = 2, k = 1; j < f.nEdges; j++, k++) {
            VERTEX const v1 = verticesPrime[f.iFirstEdge];
            VERTEX const v2 = verticesPrime[f.iFirstEdge + k];
            VERTEX const v3 = verticesPrime[f.iFirstEdge + j];
            COORDS const c1 = calcCoords(v1, b.vS, b.vT, b.fSShift, b.fTShift);
            COORDS const c2 = calcCoords(v2, b.vS, b.vT, b.fSShift, b.fTShift);
            COORDS const c3 = calcCoords(v3, b.vS, b.vT, b.fSShift, b.fTShift);

            minUV[i * 2] = std::min(minUV[i * 2], c1.u);
            minUV[i * 2 + 1] = std::min(minUV[i * 2 + 1], c1.v);
            minUV[i * 2] = std::min(minUV[i * 2], c2.u);
            minUV[i * 2 + 1] = std::min(minUV[i * 2 + 1], c2.v);
            minUV[i * 2] = std::min(minUV[i * 2], c3.u);
            minUV[i * 2 + 1] = std::min(minUV[i * 2 + 1], c3.v);

            maxUV[i * 2] = std::max(maxUV[i * 2], c1.u);
            maxUV[i * 2 + 1] = std::max(maxUV[i * 2 + 1], c1.v);
            maxUV[i * 2] = std::max(maxUV[i * 2], c2.u);
            maxUV[i * 2 + 1] = std::max(maxUV[i * 2 + 1], c2.v);
            maxUV[i * 2] = std::max(maxUV[i * 2], c3.u);
            maxUV[i * 2 + 1] = std::max(maxUV[i * 2 + 1], c3.v);
        }

        int lmw = ceil(maxUV[i * 2] / 16) - floor(minUV[i * 2] / 16) + 1;
        int lmh = ceil(maxUV[i * 2 + 1] / 16) - floor(minUV[i * 2 + 1] / 16) + 1;

        if (lmw > 17 || lmh > 17) {
            lmw = lmh = 1;
            // continue;
        }
        LMAP l{};
        l.w = lmw;
        l.h = lmh;
        if (f.nLightmapOffset < size) {
            l.offset = lmap + f.nLightmapOffset;
        } else {
            l.offset = nullptr;
        }
        lmaps.push_back(l);
    }

    int lmapRover[1024];
    memset(lmapRover, 0, 1024 * 4);

    // Light map "rover" algorithm from Quake 2 (http://fabiensanglard.net/quake2/quake2_opengl_renderer.php)
    for (u32 i = 0; i < lmaps.size(); i++) {
        int best = 1024;
        int best2 = 0;

        for (int a = 0; a < 1024 - lmaps[i].w; a++) {
            best2 = 0;
            int j = 0;
            for (j = 0; j < lmaps[i].w; j++) {
                if (lmapRover[a + j] >= best) {
                    break;
                }
                if (lmapRover[a + j] > best2) {
                    best2 = lmapRover[a + j];
                }
            }
            if (j == lmaps[i].w) {
                lmaps[i].finalX = a;
                lmaps[i].finalY = best = best2;
            }
        }

        if (best + lmaps[i].h > 1024) {
            std::cout << "Lightmap atlas is too small (" << filename << ")." << std::endl;
            break;
        }

        for (int a = 0; a < lmaps[i].w; a++) {
            lmapRover[lmaps[i].finalX + a] = best + lmaps[i].h;
        }

        int const finalX = lmaps[i].finalX;
        int const finalY = lmaps[i].finalY;

#define ATXY(_x, _y) (((_x) + ((_y)*1024)) * 3)
#define LMXY(_x, _y) (((_x) + ((_y)*lmaps[i].w)) * 3)
        for (int y = 0; y < lmaps[i].h; y++) {
            for (int x = 0; x < lmaps[i].w; x++) {
                if (lmaps[i].offset) {
                    lmapAtlas[ATXY(finalX + x, finalY + y) + 0] = gammaTable[lmaps[i].offset[LMXY(x, y) + 0]];
                    lmapAtlas[ATXY(finalX + x, finalY + y) + 1] = gammaTable[lmaps[i].offset[LMXY(x, y) + 1]];
                    lmapAtlas[ATXY(finalX + x, finalY + y) + 2] = gammaTable[lmaps[i].offset[LMXY(x, y) + 2]];
                } else {
                    lmapAtlas[ATXY(finalX + x, finalY + y) + 0] = 200;
                    lmapAtlas[ATXY(finalX + x, finalY + y) + 1] = 50;
                    lmapAtlas[ATXY(finalX + x, finalY + y) + 2] = 255;
                }
            }
        }
    }

    // Load the actual triangles

    inBSP.seekg(bHeader.lump[LUMP_FACES].nOffset, std::ios::beg);
    for (int i = 0; i < bHeader.lump[LUMP_FACES].nLength / (int)sizeof(BSPFACE); i++) {

        BSPFACE f{};
        inBSP.read((char *)&f, sizeof(f));

        if (dontRenderFace[i]) {
            continue;
        }

        BSPTEXTUREINFO const b = btfs[f.iTextureInfo];

        std::string const faceTexName = texNames[b.iMiptex];

        // Calculate light map uvs
        int const lmw = ceil(maxUV[i * 2] / 16) - floor(minUV[i * 2] / 16) + 1;
        int const lmh = ceil(maxUV[i * 2 + 1] / 16) - floor(minUV[i * 2 + 1] / 16) + 1;

        if (lmw > 17) {
            continue;
        }
        if (lmh > 17) {
            continue;
        }

        float const mid_poly_s = (minUV[i * 2] + maxUV[i * 2]) / 2.0f;
        float const mid_poly_t = (minUV[i * 2 + 1] + maxUV[i * 2 + 1]) / 2.0f;
        float const mid_tex_s = (float)lmw / 2.0f;
        float const mid_tex_t = (float)lmh / 2.0f;
        float const fX = lmaps[i].finalX;
        float const fY = lmaps[i].finalY;
        BSP_TEXTURE const t = textures[faceTexName];

        std::vector<VECFINAL> *vt = &texturedTris[faceTexName].triangles;

        for (int j = 2, k = 1; j < f.nEdges; j++, k++) {
            VERTEX v1 = verticesPrime[f.iFirstEdge];
            VERTEX v2 = verticesPrime[f.iFirstEdge + k];
            VERTEX v3 = verticesPrime[f.iFirstEdge + j];
            COORDS c1 = calcCoords(v1, b.vS, b.vT, b.fSShift, b.fTShift);
            COORDS c2 = calcCoords(v2, b.vS, b.vT, b.fSShift, b.fTShift);
            COORDS c3 = calcCoords(v3, b.vS, b.vT, b.fSShift, b.fTShift);

            COORDS c1l{};
            COORDS c2l{};
            COORDS c3l{};

            c1l.u = mid_tex_s + (c1.u - mid_poly_s) / 16.0f;
            c2l.u = mid_tex_s + (c2.u - mid_poly_s) / 16.0f;
            c3l.u = mid_tex_s + (c3.u - mid_poly_s) / 16.0f;
            c1l.v = mid_tex_t + (c1.v - mid_poly_t) / 16.0f;
            c2l.v = mid_tex_t + (c2.v - mid_poly_t) / 16.0f;
            c3l.v = mid_tex_t + (c3.v - mid_poly_t) / 16.0f;

            c1l.u += fX;
            c2l.u += fX;
            c3l.u += fX;
            c1l.v += fY;
            c2l.v += fY;
            c3l.v += fY;

            c1l.u /= 1024.0;
            c2l.u /= 1024.0;
            c3l.u /= 1024.0;
            c1l.v /= 1024.0;
            c2l.v /= 1024.0;
            c3l.v /= 1024.0;

            c1.u /= t.w;
            c2.u /= t.w;
            c3.u /= t.w;
            c1.v /= t.h;
            c2.v /= t.h;
            c3.v /= t.h;

            v1.fixHand();
            v2.fixHand();
            v3.fixHand();

            vt->push_back(VECFINAL(v1, c1, c1l));
            vt->push_back(VECFINAL(v2, c2, c2l));
            vt->push_back(VECFINAL(v3, c3, c3l));
        }
        texturedTris[faceTexName].image_id = textures[faceTexName].image_id;
    }

    delete[] btfs;
    delete[] texOffSets;
    delete[] edges;

    inBSP.close();

    lmap_image_id = device.create_image({
        .format = daxa::Format::R8G8B8A8_SRGB,
        .size = {1024, 1024, 1},
        .usage = daxa::ImageUsageFlagBits::SHADER_READ_ONLY | daxa::ImageUsageFlagBits::TRANSFER_SRC | daxa::ImageUsageFlagBits::TRANSFER_DST,
        .debug_name = "image",
    });

    BSP_TEXTURE lmap_tex;
    lmap_tex.image_id = lmap_image_id;
    lmap_tex.w = 1024;
    lmap_tex.h = 1024;
    lmap_tex.load(device, sMapEntry.m_szName + "_lightmap", lmapAtlas, 3, 4, 1);
    delete[] lmapAtlas;

    bufObjects = new BUFFER[texturedTris.size()];

    int i = 0;
    totalTris = 0;
    for (auto it = texturedTris.begin(); it != texturedTris.end(); it++, i++) {
        auto &buf = bufObjects[i];
        auto buf_size = static_cast<u32>((*it).second.triangles.size() * sizeof(VECFINAL));
        buf.buffer_id = device.create_buffer({
            .size = buf_size,
            .debug_name = "textured_tri_buffer",
        });
        upload_buffer_data(device, buf.buffer_id, reinterpret_cast<u8 *>((*it).second.triangles.data()), buf_size);
        totalTris += (*it).second.triangles.size();
    }

    mapId = id;
}

static constexpr auto parentless_maps = std::array<std::string_view, 7>{
    "c4a1",
    "c4a1a",
    "c4a2",
    "c4a1c",
    "c4a1f",
    "c4a3",
    "c5a1",
};

void BSP::calculateOffset() {
    if (offsets.contains(mapId)) {
        offset = offsets[mapId];
    } else {
        if (mapId == "c0a0" || mapId == "cs_office") {
            // Origin for other maps
            offsets[mapId] = VERTEX(0, 0, 0);
        } else {
            float ox = 0;
            float oy = 0;
            float oz = 0;
            bool found = false;

            auto parent_override = [this]() {
                if (std::find(parentless_maps.begin(), parentless_maps.end(), mapId) != parentless_maps.end()) {
                    parent_mapId = "";
                } else if (mapId == "c2a3e") {
                    parent_mapId = "c2a4";
                } else if (mapId == "c3a1a") {
                    parent_mapId = "c3a1";
                } else if (mapId == "de_dust2") {
                    parent_mapId = "cs_office";
                }
            };

            for (auto it = landmarks.begin(); it != landmarks.end(); it++) {
                if ((*it).second.size() > 1) {
                    for (size_t i = 0; i < (*it).second.size(); i++) {
                        if ((*it).second[i].second == mapId) {
                            if (i == 0) {
                                if (offsets.contains((*it).second[i + 1].second)) {
                                    VERTEX const c1 = (*it).second[i].first;
                                    VERTEX const c2 = (*it).second[i + 1].first;
                                    VERTEX const c3 = offsets[(*it).second[i + 1].second];
                                    ox = +c2.x + c3.x - c1.x;
                                    oy = +c2.y + c3.y - c1.y;
                                    oz = +c2.z + c3.z - c1.z;

                                    found = true;
                                    std::cout << "Matched " << (*it).second[i].second << " " << (*it).second[i + 1].second << std::endl;
                                    parent_mapId = (*it).second[i + 1].second;
                                    parent_override();
                                    break;
                                }
                            } else {
                                if (offsets.contains((*it).second[i - 1].second)) {
                                    VERTEX const c1 = (*it).second[i].first;
                                    VERTEX const c2 = (*it).second[i - 1].first;
                                    VERTEX const c3 = offsets[(*it).second[i - 1].second];
                                    ox = +c2.x + c3.x - c1.x;
                                    oy = +c2.y + c3.y - c1.y;
                                    oz = +c2.z + c3.z - c1.z;

                                    found = true;
                                    std::cout << "Matched " << (*it).second[i].second << " " << (*it).second[i - 1].second << std::endl;
                                    parent_mapId = (*it).second[i - 1].second;
                                    parent_override();
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            if (!found) {
                std::cout << "Cant find matching landmarks for " << mapId << std::endl;
            }
            offsets[mapId] = VERTEX(ox, oy, oz);
        }
    }
}

void BSP::render(daxa::Device &device, daxa::CommandList &cmd_list, daxa::BufferId gpu_input_buffer, daxa::SamplerId image_sampler0, daxa::SamplerId image_sampler1) {
    // Calculate map offset based on landmarks
    calculateOffset();
    f32vec3 full_offset{offset.x + ConfigOffsetChapter.x, offset.y + ConfigOffsetChapter.y, offset.z + ConfigOffsetChapter.z};

    if (!this->should_draw)
        return;
    full_offset = full_offset + propagated_user_offset;

    int i = 0;
    for (auto it = texturedTris.begin(); it != texturedTris.end(); it++, i++) {
        // Don't render some dummy triangles (triggers and such)
        if ((*it).first != "aaatrigger" && (*it).first != "origin" && (*it).first != "clip" && (*it).first != "sky" && (*it).first[0] != '{' && !(*it).second.triangles.empty()) {
            // if(mapId == "c1a0e.bsp") std::cout << (*it).first << std::endl;
            cmd_list.push_constant(DrawPush{
                .gpu_input = device.get_device_address(gpu_input_buffer),
                .vertices = device.get_device_address(bufObjects[i].buffer_id),
                .image_id0 = (*it).second.image_id.default_view(),
                .image_id1 = lmap_image_id.default_view(),
                .image_sampler0 = image_sampler0,
                .image_sampler1 = image_sampler1,
                .offset = full_offset,
            });
            auto vert_n = static_cast<u32>((*it).second.triangles.size());
            cmd_list.draw({.vertex_count = vert_n});

#if COUNT_DRAWS
            draw_count++;
#endif
        }
    }
}

void BSP::SetChapterOffset(const float x, const float y, const float z) {
    ConfigOffsetChapter.x = x;
    ConfigOffsetChapter.y = y;
    ConfigOffsetChapter.z = z;
}
