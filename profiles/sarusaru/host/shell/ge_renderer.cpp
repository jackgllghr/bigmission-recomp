#include "shell/ge_renderer.hpp"

#include "gpu/texture_decode.hpp"
#include "gpu/triangle_indices.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ge_shaders.inc"

namespace sarusaru::shell {
namespace {

// Render targets cover the PSP's 512-pixel framebuffer stride and 272 lines;
// 480 of the columns are displayed.
constexpr std::uint32_t kTargetWidth = 512u, kTargetHeight = 272u, kDisplayWidth = 480u;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkDeviceSize kVertexBytes = 32u << 20u, kUniformBytes = 16u << 20u, kStagingBytes = 32u << 20u;
constexpr std::uint32_t kFrameSets = 4096u;
constexpr std::uint64_t kTextureLifetimeFrames = 600u;
// A through-mode vertex's unused normal and w carry the tile's texture
// clamp rectangle (see ge.vert); these values leave sampling unclamped.
constexpr float kUnclamped = 1e30f;

using Matrix = std::array<float, 16>;

Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix out{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k) out[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return out;
}

constexpr Matrix kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

struct Push {
    Matrix transform;
    std::array<float, 4> viewport, texture_params, uv_transform, view_z;
};
static_assert(sizeof(Push) == 128u);

struct EnvironmentBlock {
    std::array<float, 4> ambient, fog, fog_color;
    std::array<std::array<float, 4>, 4> position, direction, attenuation, spot, light_ambient, diffuse, specular;
};
struct ObjectBlock {
    Matrix world;
    std::array<float, 4> flags, emissive, material_ambient, material_diffuse, material_specular;
};

std::array<float, 4> rgb(std::uint32_t color, float w = 1.0f) {
    return {(color & 0xFFu) / 255.0f, ((color >> 8u) & 0xFFu) / 255.0f, ((color >> 16u) & 0xFFu) / 255.0f, w};
}

// PSP blend factors 0-9; 10 is the fixed colour. Factor 0/1 names the other
// pixel's colour: the source side scales by the destination and vice versa.
VkBlendFactor blend_factor(std::uint32_t factor, bool source) {
    switch (factor) {
    case 0u: return source ? VK_BLEND_FACTOR_DST_COLOR : VK_BLEND_FACTOR_SRC_COLOR;
    case 1u: return source ? VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 2u: case 6u: return VK_BLEND_FACTOR_SRC_ALPHA;  // 6-9 are doubled forms, approximated
    case 3u: case 7u: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 4u: case 8u: return VK_BLEND_FACTOR_DST_ALPHA;
    case 5u: case 9u: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return source ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
    }
}

VkBlendOp blend_op(std::uint32_t equation) {
    switch (equation) {
    case 1u: return VK_BLEND_OP_SUBTRACT;
    case 2u: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 3u: return VK_BLEND_OP_MIN;
    case 4u: return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;  // 5 (absolute difference) is approximated
    }
}

// GE comparison functions: 0 never, 1 always, 2 equal, 3 not equal, 4 less,
// 5 less or equal, 6 greater, 7 greater or equal.
VkCompareOp compare_op(std::uint32_t function) {
    static constexpr VkCompareOp ops[8] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL,
        VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
        VK_COMPARE_OP_GREATER_OR_EQUAL};
    return ops[function & 7u];
}

} // namespace

std::uint32_t GeRenderer::canonical(std::uint32_t address) noexcept {
    address &= 0x0FFFFFFFu;
    // VRAM is mirrored every 2 MiB from 0x04000000.
    if (address >= 0x04000000u && address < 0x08000000u) return 0x04000000u | (address & 0x001FFFFFu);
    return address;
}

bool GeRenderer::create(std::string &error) {
    const auto device = vk_.device;
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = kColorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1] = attachments[0];
    attachments[1].format = vk_.depth_format;
    attachments[1].initialLayout = attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color_ref{0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref{1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass_info.attachmentCount = 2u;
    pass_info.pAttachments = attachments;
    pass_info.subpassCount = 1u;
    pass_info.pSubpasses = &subpass;
    if (vk_.vkCreateRenderPass(device, &pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
        error = "cannot create the render pass";
        return false;
    }

    VkDescriptorSetLayoutBinding texture_binding{0u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1u;
    layout_info.pBindings = &texture_binding;
    vk_.vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &texture_layout_);
    VkDescriptorSetLayoutBinding uniform_bindings[2] = {
        {0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
        {1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1u, VK_SHADER_STAGE_VERTEX_BIT},
    };
    layout_info.bindingCount = 2u;
    layout_info.pBindings = uniform_bindings;
    vk_.vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &uniform_layout_);
    const VkDescriptorSetLayout set_layouts[2] = {texture_layout_, uniform_layout_};
    VkPushConstantRange push_range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(Push)};
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 2u;
    pipeline_layout_info.pSetLayouts = set_layouts;
    pipeline_layout_info.pushConstantRangeCount = 1u;
    pipeline_layout_info.pPushConstantRanges = &push_range;
    vk_.vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout_);

    const auto shader = [&](const std::uint32_t *code, std::size_t bytes, VkShaderModule &out) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes;
        info.pCode = code;
        return vk_.vkCreateShaderModule(device, &info, nullptr, &out) == VK_SUCCESS;
    };
    if (!shader(kGeVertexShader, sizeof kGeVertexShader, vertex_shader_) ||
        !shader(kGeFragmentShader, sizeof kGeFragmentShader, fragment_shader_)) {
        error = "cannot create the GE shaders";
        return false;
    }

    VkDescriptorPoolSize persistent_sizes[1] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2u}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1u;
    pool_info.poolSizeCount = 1u;
    pool_info.pPoolSizes = persistent_sizes;
    vk_.vkCreateDescriptorPool(device, &pool_info, nullptr, &persistent_pool_);
    VkDescriptorPoolSize frame_sizes[1] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFrameSets}};
    pool_info.maxSets = kFrameSets;
    pool_info.pPoolSizes = frame_sizes;
    vk_.vkCreateDescriptorPool(device, &pool_info, nullptr, &frame_pool_);

    if (!vk_.create_buffer(kVertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices_) ||
        !vk_.create_buffer(kUniformBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniforms_) ||
        !vk_.create_buffer(kStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_)) {
        error = "cannot allocate the renderer's buffers";
        return false;
    }
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = persistent_pool_;
    allocate.descriptorSetCount = 1u;
    allocate.pSetLayouts = &uniform_layout_;
    vk_.vkAllocateDescriptorSets(device, &allocate, &uniform_set_);
    VkDescriptorBufferInfo environment{uniforms_.buffer, 0u, sizeof(EnvironmentBlock)};
    VkDescriptorBufferInfo object{uniforms_.buffer, 0u, sizeof(ObjectBlock)};
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = uniform_set_;
        writes[i].dstBinding = static_cast<std::uint32_t>(i);
        writes[i].descriptorCount = 1u;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[i].pBufferInfo = i == 0 ? &environment : &object;
    }
    vk_.vkUpdateDescriptorSets(device, 2u, writes, 0u, nullptr);

    const VkImageUsageFlags texture_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!vk_.create_image(1u, 1u, kColorFormat, texture_usage, VK_IMAGE_ASPECT_COLOR_BIT, white_) ||
        !vk_.create_image(kTargetWidth, kTargetHeight, kColorFormat, texture_usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, upload_)) {
        error = "cannot create the renderer's images";
        return false;
    }
    return true;
}

void GeRenderer::destroy() {
    const auto device = vk_.device;
    if (device == VK_NULL_HANDLE) return;
    vk_.vkDeviceWaitIdle(device);
    for (auto &layer : targets_) {
        for (auto &[address, target] : layer) {
            (void)address;
            vk_.vkDestroyFramebuffer(device, target->framebuffer, nullptr);
            vk_.destroy_image(target->color);
            vk_.destroy_image(target->depth);
        }
        layer.clear();
    }
    queued_.clear();
    queued_count_ = 0u;
    for (auto &[key, texture] : textures_) {
        (void)key;
        vk_.destroy_image(texture.image);
    }
    textures_.clear();
    for (auto &[key, texture] : host_textures_) vk_.destroy_image(texture.image);
    host_textures_.clear();
    for (auto &[key, pipeline] : pipelines_) {
        (void)key;
        vk_.vkDestroyPipeline(device, pipeline, nullptr);
    }
    for (auto &[key, sampler] : samplers_) {
        (void)key;
        vk_.vkDestroySampler(device, sampler, nullptr);
    }
    vk_.destroy_image(white_);
    vk_.destroy_image(upload_);
    vk_.destroy_buffer(vertices_);
    vk_.destroy_buffer(uniforms_);
    vk_.destroy_buffer(staging_);
    vk_.vkDestroyDescriptorPool(device, frame_pool_, nullptr);
    vk_.vkDestroyDescriptorPool(device, persistent_pool_, nullptr);
    vk_.vkDestroyShaderModule(device, vertex_shader_, nullptr);
    vk_.vkDestroyShaderModule(device, fragment_shader_, nullptr);
    vk_.vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    vk_.vkDestroyDescriptorSetLayout(device, texture_layout_, nullptr);
    vk_.vkDestroyDescriptorSetLayout(device, uniform_layout_, nullptr);
    vk_.vkDestroyRenderPass(device, render_pass_, nullptr);
}

void GeRenderer::begin_frame(VkCommandBuffer commands) {
    commands_ = commands;
    if (trace_ != 0u) --trace_;
    vertex_cursor_ = uniform_cursor_ = staging_cursor_ = 0u;
    vk_.vkResetDescriptorPool(vk_.device, frame_pool_, 0u);
    frame_sets_ = 0u;
    pass_target_ = nullptr;
    ++frame_;
    // The previous frame has completed, so unused textures can go.
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (frame_ - it->second.last_frame > kTextureLifetimeFrames) {
            vk_.destroy_image(it->second.image);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
    if (!white_ready_) {
        const std::uint32_t white = 0xFFFFFFFFu;
        upload(white_, &white, 1u, 1u);
        white_ready_ = true;
    }
}

void GeRenderer::flush() {
    end_pass();
    commands_ = vk_.flush();
    vertex_cursor_ = uniform_cursor_ = staging_cursor_ = 0u;
    vk_.vkResetDescriptorPool(vk_.device, frame_pool_, 0u);
    frame_sets_ = 0u;
}

VkDeviceSize GeRenderer::reserve(Buffer &buffer, VkDeviceSize &cursor, VkDeviceSize bytes, VkDeviceSize alignment) {
    VkDeviceSize offset = (cursor + alignment - 1u) / alignment * alignment;
    if (offset + bytes > buffer.size) {
        flush();
        offset = 0u;
    }
    cursor = offset + bytes;
    return offset;
}

GeRenderer::Target *GeRenderer::target(std::size_t layer, std::uint32_t address) {
    const std::uint32_t key = canonical(address);
    if (const auto found = targets_[layer].find(key); found != targets_[layer].end()) return found->second.get();
    end_pass();
    auto target = std::make_unique<Target>();
    target->address = key;
    const std::uint32_t width = kTargetWidth * scale_, height = kTargetHeight * scale_;
    if (!vk_.create_image(width, height, kColorFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, target->color) ||
        !vk_.create_image(width, height, vk_.depth_format,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT, target->depth))
        return nullptr;
    const VkImageView views[2] = {target->color.view, target->depth.view};
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = render_pass_;
    info.attachmentCount = 2u;
    info.pAttachments = views;
    info.width = width;
    info.height = height;
    info.layers = 1u;
    vk_.vkCreateFramebuffer(vk_.device, &info, nullptr, &target->framebuffer);
    // New targets start black with depth 0 (the farthest, as the GE counts).
    vk_.transition(commands_, target->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearColorValue black{};
    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vk_.vkCmdClearColorImage(commands_, target->color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1u, &color_range);
    vk_.transition(commands_, target->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    target->color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vk_.transition(commands_, target->depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearDepthStencilValue far{0.0f, 0u};
    const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0u, 1u, 0u, 1u};
    vk_.vkCmdClearDepthStencilImage(commands_, target->depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1u, &depth_range);
    vk_.transition(commands_, target->depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    Target *raw = target.get();
    targets_[layer].emplace(key, std::move(target));
    return raw;
}

void GeRenderer::begin_pass(Target &target) {
    if (target.color_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        vk_.transition(commands_, target.color.image, VK_IMAGE_ASPECT_COLOR_BIT, target.color_layout,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        target.color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = render_pass_;
    info.framebuffer = target.framebuffer;
    info.renderArea = {{0, 0}, {target.color.width, target.color.height}};
    vk_.vkCmdBeginRenderPass(commands_, &info, VK_SUBPASS_CONTENTS_INLINE);
    pass_target_ = &target;
}

void GeRenderer::end_pass() {
    if (pass_target_ == nullptr) return;
    vk_.vkCmdEndRenderPass(commands_);
    pass_target_ = nullptr;
}

void GeRenderer::upload(Image &image, const std::uint32_t *texels, std::uint32_t width, std::uint32_t height) {
    end_pass();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4u;
    const VkDeviceSize offset = reserve(staging_, staging_cursor_, bytes, 16u);
    std::memcpy(static_cast<std::uint8_t *>(staging_.mapped) + offset, texels, bytes);
    vk_.transition(commands_, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.bufferOffset = offset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    copy.imageExtent = {width, height, 1u};
    vk_.vkCmdCopyBufferToImage(commands_, staging_.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
    vk_.transition(commands_, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

VkSampler GeRenderer::sampler(const gpu::TextureState &texture) {
    const std::uint32_t key = (texture.min_filter & 1u) | ((texture.mag_filter & 1u) << 1u) |
                              ((texture.wrap_s & 1u) << 2u) | ((texture.wrap_t & 1u) << 3u);
    if (const auto found = samplers_.find(key); found != samplers_.end()) return found->second;
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = (key & 2u) != 0u ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = (key & 1u) != 0u ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.addressModeU = (key & 4u) != 0u ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = (key & 8u) != 0u ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0f;
    VkSampler sampler = VK_NULL_HANDLE;
    vk_.vkCreateSampler(vk_.device, &info, nullptr, &sampler);
    samplers_.emplace(key, sampler);
    return sampler;
}

VkImageView GeRenderer::texture_view(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call, float &u_scale,
                                     float &v_scale) {
    const auto &texture = call.texture;
    u_scale = v_scale = 1.0f;
    if (translation_pack_ != nullptr) {
        const auto pixels = translation_pack_->host_rgba(texture.address);
        if (!pixels.empty()) {
            if (const auto found = host_textures_.find(texture.address); found != host_textures_.end()) return found->second.image.view;
            Texture entry;
            if (!vk_.create_image(512, 16, kColorFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, entry.image)) return VK_NULL_HANDLE;
            std::vector<std::uint32_t> texels(pixels.size() / 4);
            for (std::size_t i = 0; i < texels.size(); ++i)
                for (unsigned byte = 0; byte < 4; ++byte)
                    texels[i] |= static_cast<std::uint32_t>(pixels[i * 4 + byte]) << (byte * 8);
            upload(entry.image, texels.data(), 512, 16);
            const auto view = entry.image.view;
            host_textures_.emplace(texture.address, std::move(entry));
            return view;
        }
    }
    if (texture.width == 0u || texture.height == 0u) return VK_NULL_HANDLE;
    // Render to texture: sample the GPU target drawn at that address.
    const auto &game = targets_[kGame];
    if (const auto found = game.find(canonical(texture.address)); found != game.end() &&
        found->first != canonical(call.target.color_address))
        return sample_target(*found->second, texture, u_scale, v_scale);
    const std::uint64_t key = gpu::texture_key(memory, texture);
    if (const auto found = textures_.find(key); found != textures_.end()) {
        found->second.last_frame = frame_;
        return found->second.image.view;
    }
    std::vector<std::uint32_t> texels;
    if (!gpu::decode_texture(memory, texture, texels) || texels.size() < static_cast<std::size_t>(texture.width) * texture.height)
        return VK_NULL_HANDLE;
    if (translation_pack_ != nullptr)
        translation_pack_->apply(texture, texels);
    Texture entry;
    if (!vk_.create_image(texture.width, texture.height, kColorFormat,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, entry.image))
        return VK_NULL_HANDLE;
    upload(entry.image, texels.data(), texture.width, texture.height);
    entry.last_frame = frame_;
    const VkImageView view = entry.image.view;
    textures_.emplace(key, std::move(entry));
    return view;
}

VkImageView GeRenderer::sample_target(Target &source, const gpu::TextureState &texture, float &u_scale, float &v_scale) {
    if (source.color_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        end_pass();
        vk_.transition(commands_, source.color.image, VK_IMAGE_ASPECT_COLOR_BIT, source.color_layout,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        source.color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    u_scale = static_cast<float>(texture.width) / kTargetWidth;
    v_scale = static_cast<float>(texture.height) / kTargetHeight;
    return source.color.view;
}

VkDescriptorSet GeRenderer::texture_set(VkImageView view, VkSampler sampler) {
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = frame_pool_;
    allocate.descriptorSetCount = 1u;
    allocate.pSetLayouts = &texture_layout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    vk_.vkAllocateDescriptorSets(vk_.device, &allocate, &set);
    ++frame_sets_;
    VkDescriptorImageInfo image{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1u;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vk_.vkUpdateDescriptorSets(vk_.device, 1u, &write, 0u, nullptr);
    return set;
}

VkPipeline GeRenderer::pipeline(const PipelineKey &key) {
    if (const auto found = pipelines_.find(key); found != pipelines_.end()) return found->second;
    VkPipelineShaderStageCreateInfo stages[2]{};
    const VkBool32 alpha_test = (key.raster & 8u) != 0u ? VK_TRUE : VK_FALSE;
    VkSpecializationMapEntry entry{0u, 0u, sizeof(VkBool32)};
    VkSpecializationInfo specialization{1u, &entry, sizeof(VkBool32), &alpha_test};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT, vertex_shader_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader_, "main", &specialization};
    VkVertexInputBindingDescription binding{0u, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[4] = {
        {0u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, position)},
        {1u, 0u, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, texcoord)},
        {2u, 0u, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GpuVertex, color)},
        {3u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, normal)},
    };
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1u;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 4u;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = static_cast<VkPrimitiveTopology>(key.topology);
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1u;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = static_cast<VkCullModeFlags>(key.raster & 3u);
    raster.frontFace = (key.raster & 4u) != 0u ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = (key.depth & 1u) != 0u;
    depth.depthCompareOp = static_cast<VkCompareOp>((key.depth >> 1u) & 7u);
    depth.depthWriteEnable = (key.depth & 16u) != 0u;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = (key.blend & 1u) != 0u;
    attachment.srcColorBlendFactor = static_cast<VkBlendFactor>((key.blend >> 1u) & 31u);
    attachment.dstColorBlendFactor = static_cast<VkBlendFactor>((key.blend >> 6u) & 31u);
    attachment.colorBlendOp = static_cast<VkBlendOp>((key.blend >> 11u) & 7u);
    // The framebuffer alpha holds the GE's stencil value: written, not blended.
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = (key.blend >> 14u) & 15u;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1u;
    blend.pAttachments = &attachment;
    const VkDynamicState dynamic_states[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 3u;
    dynamic.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2u;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline_layout_;
    info.renderPass = render_pass_;
    VkPipeline pipeline = VK_NULL_HANDLE;
    vk_.vkCreateGraphicsPipelines(vk_.device, VK_NULL_HANDLE, 1u, &info, nullptr, &pipeline);
    pipelines_.emplace(key, pipeline);
    return pipeline;
}

void GeRenderer::draw(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call) {
    draw_into(kGame, &memory, call, nullptr);
}

void GeRenderer::queue_interpolated(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call) {
    if (call.vertices.empty() || call.primitive == gpu::PrimitiveType::Points) return;
    if (queued_count_ == queued_.size()) queued_.emplace_back();
    QueuedDraw &queued = queued_[queued_count_++];
    queued.call = call;
    queued.texture = {};
    if (!call.texture.enabled || call.clear_mode || call.texture.width == 0u || call.texture.height == 0u) return;
    const auto &game = targets_[kGame];
    if (const auto found = game.find(canonical(call.texture.address)); found != game.end() &&
        found->first != canonical(call.target.color_address)) {
        queued.texture.render_target = true;
        return;
    }
    // The game's own draw has just looked this texture up, so this finds it
    // in the cache: it is the texture as guest memory holds it now.
    queued.texture.view = texture_view(memory, call, queued.texture.u_scale, queued.texture.v_scale);
}

void GeRenderer::draw_interpolated() {
    for (std::size_t i = 0; i < queued_count_; ++i) draw_into(kInterpolated, nullptr, queued_[i].call, &queued_[i].texture);
    queued_count_ = 0u;
}

void GeRenderer::draw_into(std::size_t layer, const psprecomp::GuestMemory *memory, const gpu::DrawCall &call,
                           const QueuedTexture *queued) {
    if (trace_ && memory != nullptr) {
        const auto &memory_ref = *memory;
        const auto &v = call.viewport;
        std::fprintf(stderr,
            "draw prim=%u n=%zu idx=%zu through=%d target=%08x tex=%d %08x %ux%u fmt=%u tfx=%u tcc=%d clear=%d/%u "
            "blend=%d %u/%u eq%u depth=%d f%u w%d alpha=%d f%u ref%u cull=%d/%d light=%d fog=%d color=%d mat=%08x "
            "vp=%.0f,%.0f,%.0f s=%.0f,%.0f,%.0f off=%.0f,%.0f sc=%u,%u-%u,%u cmd=%08x\n",
            static_cast<unsigned>(call.primitive), call.vertices.size(), call.indices.size(), call.through,
            call.target.color_address, call.texture.enabled, call.texture.address, call.texture.width, call.texture.height,
            static_cast<unsigned>(call.texture.format), call.texture.function, call.texture.alpha_from_texture,
            call.clear_mode, call.clear_flags, call.blend.enabled, call.blend.source_factor, call.blend.destination_factor,
            call.blend.equation, call.depth.test_enabled, call.depth.function, call.depth.write_enabled,
            call.alpha_test.enabled, call.alpha_test.function, call.alpha_test.reference, call.culling_enabled,
            call.cull_clockwise, call.lighting_enabled, call.fog.enabled, call.has_vertex_color, call.material_color,
            v.x_offset, v.y_offset, v.z_offset, v.x_scale, v.y_scale, v.z_scale, v.offset_x, v.offset_y, v.scissor_x1,
            v.scissor_y1, v.scissor_x2, v.scissor_y2, call.command_address);
        const auto &t = call.texture;
        std::vector<std::uint32_t> texels;
        const bool decoded = t.enabled && gpu::decode_texture(memory_ref, t, texels);
        std::uint64_t opaque = 0u, black = 0u;
        for (const auto texel : texels) {
            opaque += (texel >> 24u) != 0u;
            black += (texel & 0xFFFFFFu) == 0u;
        }
        std::fprintf(stderr, "  clut=%08x fmt=%u shift=%u mask=%02x off=%u load=%u bw=%u swz=%d decoded=%d texels=%zu opaque=%llu black=%llu\n",
                     t.clut_address, t.clut_format, t.clut_shift, t.clut_mask, t.clut_offset, t.clut_load_bytes,
                     t.buffer_width, t.swizzled, decoded, texels.size(), static_cast<unsigned long long>(opaque),
                     static_cast<unsigned long long>(black));
        if (!call.vertices.empty()) {
            const auto &p = call.vertices.front().position;
            const Matrix vw = multiply(call.view, call.world);
            std::fprintf(stderr, "  v0=%.2f,%.2f,%.2f,%.2f c0=%08x amb=%06x/%02x upd=%u lights=%d%d%d%d fog end=%g scale=%g color=%06x viewz=%.3f\n", p[0], p[1], p[2], p[3],
                         call.vertices.front().color, call.lighting.ambient_color, call.lighting.ambient_alpha,
                         call.lighting.material_update, call.lighting.lights[0].enabled, call.lighting.lights[1].enabled,
                         call.lighting.lights[2].enabled, call.lighting.lights[3].enabled,
                         call.fog.end, call.fog.scale, call.fog.color,
                         vw[2] * p[0] + vw[6] * p[1] + vw[10] * p[2] + vw[14]);
        }
    }
    if (call.vertices.empty() || call.primitive == gpu::PrimitiveType::Points) return;
    Target *target = this->target(layer, call.target.color_address);
    if (target == nullptr) return;
    const float scale = static_cast<float>(scale_);

    // Texture first: uploading may end the pass or flush the frame.
    const bool textured = call.texture.enabled && !call.clear_mode;
    float u_scale = 1.0f, v_scale = 1.0f;
    VkImageView view = VK_NULL_HANDLE;
    if (textured && queued == nullptr) {
        view = texture_view(*memory, call, u_scale, v_scale);
    } else if (textured && queued->render_target) {
        // The in-between picture samples in-between render targets; the
        // game's own is the fallback for one only the game has drawn yet.
        const std::uint32_t address = canonical(call.texture.address);
        Target *source = nullptr;
        if (address == canonical(call.target.color_address)) return;
        for (const std::size_t from : {layer, std::size_t{kGame}})
            if (const auto found = targets_[from].find(address); source == nullptr && found != targets_[from].end())
                source = found->second.get();
        if (source != nullptr) view = sample_target(*source, call.texture, u_scale, v_scale);
    } else if (textured) {
        view = queued->view;
        u_scale = queued->u_scale;
        v_scale = queued->v_scale;
    }
    const bool sampling = view != VK_NULL_HANDLE;
    if (!sampling) view = white_.view;
    const VkSampler texture_sampler = sampler(call.texture);

    // Sprites become screen-space quads; everything else stays as decoded.
    const bool sprites = call.primitive == gpu::PrimitiveType::Sprites;
    const bool through = call.through || sprites;
    const Matrix view_world = multiply(call.view, call.world);
    const Matrix wvp = multiply(call.projection, view_world);
    const auto &viewport = call.viewport;
    const auto colour_of = [&](const gpu::Vertex &vertex) {
        return call.has_vertex_color || call.clear_mode ? vertex.color : call.material_color;
    };
    const auto convert = [&](const gpu::Vertex &vertex) {
        GpuVertex out{};
        out.color = colour_of(vertex);
        out.texcoord[0] = vertex.texcoord[0];
        out.texcoord[1] = vertex.texcoord[1];
        if (through) {
            out.position[0] = vertex.position[0];
            out.position[1] = vertex.position[1];
            out.position[2] = vertex.position[2];
            out.position[3] = kUnclamped;
            out.normal[0] = out.normal[1] = -kUnclamped;
            out.normal[2] = kUnclamped;
        } else {
            for (int i = 0; i < 3; ++i) {
                out.position[i] = vertex.position[i];
                out.normal[i] = vertex.normal[i];
            }
            out.position[3] = 1.0f;
        }
        return out;
    };
    std::vector<GpuVertex> out;
    const std::size_t count = call.indices.empty() ? call.vertices.size() : call.indices.size();
    const auto vertex_at = [&](std::size_t i) -> const gpu::Vertex & {
        return call.vertices[std::min<std::size_t>(gpu::primitive_vertex(call.indices, call.vertices.size(), i), call.vertices.size() - 1u)];
    };
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (sprites) {
        // A sprite is two corners; the second gives its colour. Transformed
        // sprites are projected to the screen first.
        const auto screen = [&](const gpu::Vertex &vertex) {
            GpuVertex v = convert(vertex);
            if (!call.through) {
                const auto &p = vertex.position;
                std::array<float, 4> clip{};
                for (int row = 0; row < 4; ++row)
                    clip[row] = wvp[row] * p[0] + wvp[4 + row] * p[1] + wvp[8 + row] * p[2] + wvp[12 + row];
                const float w = clip[3] != 0.0f ? clip[3] : 1.0f;
                v.position[0] = viewport.x_offset - viewport.offset_x + clip[0] / w * viewport.x_scale;
                v.position[1] = viewport.y_offset - viewport.offset_y + clip[1] / w * viewport.y_scale;
                v.position[2] = viewport.z_offset + clip[2] / w * viewport.z_scale;
                v.texcoord[0] = (vertex.texcoord[0] * call.texture.scale_u + call.texture.offset_u) * call.texture.width;
                v.texcoord[1] = (vertex.texcoord[1] * call.texture.scale_v + call.texture.offset_v) * call.texture.height;
            }
            return v;
        };
        for (std::size_t i = 0; i + 1u < count; i += 2u) {
            const GpuVertex a = screen(vertex_at(i)), b = screen(vertex_at(i + 1u));
            GpuVertex corners[4] = {b, b, b, b};
            corners[0].position[0] = a.position[0]; corners[0].position[1] = a.position[1];
            corners[0].texcoord[0] = a.texcoord[0]; corners[0].texcoord[1] = a.texcoord[1];
            corners[1].position[1] = a.position[1]; corners[1].texcoord[1] = a.texcoord[1];
            corners[2].position[0] = a.position[0]; corners[2].texcoord[0] = a.texcoord[0];
            for (const int index : {0, 1, 2, 1, 3, 2}) out.push_back(corners[index]);
        }
    } else if (call.primitive == gpu::PrimitiveType::Lines || call.primitive == gpu::PrimitiveType::LineStrip) {
        topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        const bool strip = call.primitive == gpu::PrimitiveType::LineStrip;
        for (std::size_t i = 0; i + 1u < count; i += strip ? 1u : 2u) {
            out.push_back(convert(vertex_at(i)));
            out.push_back(convert(vertex_at(i + 1u)));
        }
    } else {
        std::vector<std::uint16_t> triangles;
        if (!gpu::triangle_indices(call.primitive, count, call.indices, call.vertices.size(), triangles)) return;
        out.reserve(triangles.size());
        for (const auto index : triangles) out.push_back(convert(call.vertices[index]));
    }
    if (out.empty()) return;

    // Per-frame space for everything the draw needs, flushing once if short.
    const VkDeviceSize uniform_alignment = std::max<VkDeviceSize>(vk_.properties.limits.minUniformBufferOffsetAlignment, 16u);
    const VkDeviceSize vertex_bytes = out.size() * sizeof(GpuVertex);
    const VkDeviceSize uniform_bytes = 2u * (std::max<VkDeviceSize>(sizeof(EnvironmentBlock), sizeof(ObjectBlock)) + uniform_alignment);
    if (vertex_cursor_ + vertex_bytes > vertices_.size || uniform_cursor_ + uniform_bytes > uniforms_.size ||
        frame_sets_ >= kFrameSets)
        flush();
    const VkDeviceSize vertex_offset = reserve(vertices_, vertex_cursor_, vertex_bytes, sizeof(float));
    std::memcpy(static_cast<std::uint8_t *>(vertices_.mapped) + vertex_offset, out.data(), vertex_bytes);

    EnvironmentBlock environment{};
    ObjectBlock object{};
    const bool lighting = !through && call.lighting_enabled && !call.clear_mode;
    const bool fog = !through && call.fog.enabled && !call.clear_mode;
    const auto &light = call.lighting;
    environment.ambient = rgb(light.ambient_color, static_cast<float>(light.ambient_alpha & 0xFFu) / 255.0f);
    // The GE has no infinities or NaNs: this game sets fog end to infinity
    // and its scale to NaN, which hardware treats as large finite values (the
    // PPSSPP workaround: 65535 with the value's sign), leaving the scene clear.
    const auto finite = [](float value) {
        return std::isfinite(value) ? value : (std::signbit(value) ? -65535.0f : 65535.0f);
    };
    environment.fog = {finite(call.fog.end), finite(call.fog.scale), 0.0f, 0.0f};
    environment.fog_color = rgb(call.fog.color);
    for (std::size_t i = 0; i < 4u; ++i) {
        const auto &l = light.lights[i];
        environment.position[i] = {l.position[0], l.position[1], l.position[2], l.enabled ? 1.0f : 0.0f};
        environment.direction[i] = {l.direction[0], l.direction[1], l.direction[2], static_cast<float>(l.type)};
        environment.attenuation[i] = {l.attenuation[0], l.attenuation[1], l.attenuation[2], static_cast<float>(l.kind)};
        environment.spot[i] = {l.spot_exponent, l.spot_cutoff, 0.0f, 0.0f};
        environment.light_ambient[i] = rgb(l.ambient);
        environment.diffuse[i] = rgb(l.diffuse);
        environment.specular[i] = rgb(l.specular);
    }
    object.world = call.world;
    object.flags = {0.0f, call.has_vertex_color ? 1.0f : 0.0f, 0.0f, static_cast<float>(light.material_update & 7u)};
    object.emissive = rgb(light.material_emissive, light.specular_power);
    object.material_ambient = rgb(call.material_color, static_cast<float>(call.material_color >> 24u) / 255.0f);
    object.material_diffuse = rgb(light.material_diffuse, light.mode == 1u ? 1.0f : 0.0f);
    object.material_specular = rgb(light.material_specular, light.reverse_normals ? 1.0f : 0.0f);
    const VkDeviceSize environment_offset = reserve(uniforms_, uniform_cursor_, sizeof environment, uniform_alignment);
    std::memcpy(static_cast<std::uint8_t *>(uniforms_.mapped) + environment_offset, &environment, sizeof environment);
    const VkDeviceSize object_offset = reserve(uniforms_, uniform_cursor_, sizeof object, uniform_alignment);
    std::memcpy(static_cast<std::uint8_t *>(uniforms_.mapped) + object_offset, &object, sizeof object);
    const VkDescriptorSet set = texture_set(view, texture_sampler);

    // Pipeline state.
    PipelineKey key;
    key.topology = static_cast<std::uint32_t>(topology);
    const auto &b = call.blend;
    std::uint32_t write_mask = 0xFu;
    float constants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (call.clear_mode) {
        // clear_flags: bit 0 colour, 1 alpha (stencil), 2 depth.
        write_mask = ((call.clear_flags & 1u) != 0u ? 7u : 0u) | ((call.clear_flags & 2u) != 0u ? 8u : 0u);
        key.blend = write_mask << 14u;
        key.depth = 1u | (static_cast<std::uint32_t>(VK_COMPARE_OP_ALWAYS) << 1u) | ((call.clear_flags & 4u) != 0u ? 16u : 0u);
    } else {
        std::uint32_t source = blend_factor(b.source_factor, true), destination = blend_factor(b.destination_factor, false);
        // A fixed colour of white or black is just one or zero; otherwise the
        // blend constant carries it (one per draw: the source's, if both are fixed).
        const auto fixed = [&](std::uint32_t factor, std::uint32_t color, std::uint32_t &out) {
            if (factor != 10u) return;
            const std::uint32_t value = color & 0x00FFFFFFu;
            if (value == 0x00FFFFFFu) out = VK_BLEND_FACTOR_ONE;
            else if (value == 0u) out = VK_BLEND_FACTOR_ZERO;
            else {
                out = VK_BLEND_FACTOR_CONSTANT_COLOR;
                const auto c = rgb(value);
                std::copy(c.begin(), c.end(), constants);
            }
        };
        fixed(b.destination_factor, b.fixed_destination, destination);
        fixed(b.source_factor, b.fixed_source, source);
        key.blend = (b.enabled ? 1u : 0u) | (source << 1u) | (destination << 6u) |
                    (static_cast<std::uint32_t>(blend_op(b.equation)) << 11u) | (write_mask << 14u);
        const bool depth_test = call.depth.test_enabled;
        key.depth = (depth_test ? 1u : 0u) | (static_cast<std::uint32_t>(compare_op(call.depth.function)) << 1u) |
                    (depth_test && call.depth.write_enabled ? 16u : 0u);
    }
    if (!through && !call.clear_mode && call.culling_enabled && topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        // With CULL's bit set the faces kept are the ones Vulkan sees as
        // clockwise, and counter-clockwise ones otherwise (checked against the
        // game's scenery, which vanishes with the opposite mapping).
        key.raster = static_cast<std::uint32_t>(VK_CULL_MODE_BACK_BIT) | (call.cull_clockwise ? 0u : 4u);
    // GE alpha functions are 0 never and 1 always; ge.frag treats 0 as "no
    // test" and 1 as "never", so the two are swapped in the push constants.
    const std::uint32_t alpha_function = call.alpha_test.function & 7u;
    const bool alpha_test = call.alpha_test.enabled && alpha_function != 1u && !call.clear_mode;
    if (alpha_test) key.raster |= 8u;
    const float shader_alpha_function = !alpha_test ? 0.0f : alpha_function == 0u ? 1.0f : static_cast<float>(alpha_function);

    Push push{};
    push.transform = through ? kIdentity : wvp;
    push.viewport = {static_cast<float>(kTargetWidth), static_cast<float>(kTargetHeight), through ? 1.0f : 0.0f,
                     static_cast<float>((fog ? 1 : 0) | (lighting ? 2 : 0))};
    // The texture function, plus 8 when the texture's alpha is ignored (TCC off).
    const std::uint32_t texture_function = (call.texture.function & 7u) | (call.texture.alpha_from_texture ? 0u : 8u);
    push.texture_params = {sampling ? 1.0f : 0.0f, static_cast<float>(texture_function),
                           static_cast<float>(call.alpha_test.reference), shader_alpha_function};
    if (call.clear_mode) push.texture_params = {0.0f, 0.0f, 0.0f, 0.0f};
    const float width = std::max<float>(call.texture.width, 1.0f), height = std::max<float>(call.texture.height, 1.0f);
    push.uv_transform = through ? std::array<float, 4>{u_scale / width, v_scale / height, 0.0f, 0.0f}
                                : std::array<float, 4>{call.texture.scale_u * u_scale, call.texture.scale_v * v_scale,
                                                       call.texture.offset_u * u_scale, call.texture.offset_v * v_scale};
    push.view_z = {view_world[2], view_world[6], view_world[10], view_world[14]};

    VkViewport vp{};
    if (through) {
        vp = {0.0f, 0.0f, kTargetWidth * scale, kTargetHeight * scale, 0.0f, 1.0f};
    } else {
        vp.x = (viewport.x_offset - viewport.offset_x - viewport.x_scale) * scale;
        vp.y = (viewport.y_offset - viewport.offset_y - viewport.y_scale) * scale;
        vp.width = 2.0f * viewport.x_scale * scale;
        vp.height = 2.0f * viewport.y_scale * scale;
        vp.minDepth = std::clamp((viewport.z_offset - viewport.z_scale) / 65535.0f, 0.0f, 1.0f);
        vp.maxDepth = std::clamp((viewport.z_offset + viewport.z_scale) / 65535.0f, 0.0f, 1.0f);
        if (vp.width == 0.0f || vp.height == 0.0f) return;
    }
    const auto clamp_x = [&](std::uint32_t x) { return std::min(x, kTargetWidth); };
    const auto clamp_y = [&](std::uint32_t y) { return std::min(y, kTargetHeight); };
    const std::uint32_t x1 = clamp_x(viewport.scissor_x1), y1 = clamp_y(viewport.scissor_y1);
    const std::uint32_t x2 = clamp_x(viewport.scissor_x2 + 1u), y2 = clamp_y(viewport.scissor_y2 + 1u);
    if (x2 <= x1 || y2 <= y1) return;
    const VkRect2D scissor{{static_cast<std::int32_t>(x1 * scale_), static_cast<std::int32_t>(y1 * scale_)},
                           {(x2 - x1) * scale_, (y2 - y1) * scale_}};

    if (pass_target_ != target) {
        end_pass();
        begin_pass(*target);
    }
    vk_.vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline(key));
    const VkDescriptorSet sets[2] = {set, uniform_set_};
    const std::uint32_t offsets[2] = {static_cast<std::uint32_t>(environment_offset), static_cast<std::uint32_t>(object_offset)};
    vk_.vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0u, 2u, sets, 2u, offsets);
    vk_.vkCmdPushConstants(commands_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u,
                           sizeof push, &push);
    vk_.vkCmdSetViewport(commands_, 0u, 1u, &vp);
    vk_.vkCmdSetScissor(commands_, 0u, 1u, &scissor);
    vk_.vkCmdSetBlendConstants(commands_, constants);
    const VkDeviceSize vertex_buffer_offset = vertex_offset;
    vk_.vkCmdBindVertexBuffers(commands_, 0u, 1u, &vertices_.buffer, &vertex_buffer_offset);
    vk_.vkCmdDraw(commands_, static_cast<std::uint32_t>(out.size()), 1u, 0u, 0u);
    ++draws_;
}

const Image *GeRenderer::presentable(const psprecomp::GuestMemory &memory, const display::Display::Framebuffer &framebuffer,
                                     std::uint32_t &width, std::uint32_t &height, bool interpolated) {
    end_pass();
    if (framebuffer.address == 0u) return nullptr;
    auto *layer = &targets_[kGame];
    if (interpolated && targets_[kInterpolated].contains(canonical(framebuffer.address))) layer = &targets_[kInterpolated];
    if (const auto found = layer->find(canonical(framebuffer.address)); found != layer->end()) {
        Target &target = *found->second;
        vk_.transition(commands_, target.color.image, VK_IMAGE_ASPECT_COLOR_BIT, target.color_layout,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        target.color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        width = kDisplayWidth * scale_;
        height = kTargetHeight * scale_;
        return &target.color;
    }
    // Drawn by the CPU: convert the guest framebuffer.
    const std::uint32_t stride = std::max(framebuffer.buffer_width, kDisplayWidth);
    const std::uint32_t bytes_per_pixel = framebuffer.pixel_format == 3u ? 4u : 2u;
    if (!memory.contains(framebuffer.address, static_cast<std::size_t>(stride) * kTargetHeight * bytes_per_pixel)) return nullptr;
    std::vector<std::uint32_t> texels(static_cast<std::size_t>(kTargetWidth) * kTargetHeight, 0xFF000000u);
    for (std::uint32_t y = 0; y < kTargetHeight; ++y) {
        for (std::uint32_t x = 0; x < kDisplayWidth; ++x) {
            const std::uint32_t at = framebuffer.address + (y * stride + x) * bytes_per_pixel;
            std::uint32_t rgba = 0u;
            if (bytes_per_pixel == 4u) {
                rgba = memory.load32(at) | 0xFF000000u;
            } else {
                const std::uint32_t p = memory.load16(at);
                std::uint32_t r{}, g{}, b{};
                switch (framebuffer.pixel_format) {
                case 0u: r = (p & 31u) << 3u; g = ((p >> 5u) & 63u) << 2u; b = ((p >> 11u) & 31u) << 3u; break;
                case 1u: r = (p & 31u) << 3u; g = ((p >> 5u) & 31u) << 3u; b = ((p >> 10u) & 31u) << 3u; break;
                default: r = (p & 15u) << 4u; g = ((p >> 4u) & 15u) << 4u; b = ((p >> 8u) & 15u) << 4u; break;
                }
                rgba = r | (g << 8u) | (b << 16u) | 0xFF000000u;
            }
            texels[static_cast<std::size_t>(y) * kTargetWidth + x] = rgba;
        }
    }
    upload(upload_, texels.data(), kTargetWidth, kTargetHeight);
    vk_.transition(commands_, upload_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    width = kDisplayWidth;
    height = kTargetHeight;
    return &upload_;
}

} // namespace sarusaru::shell
