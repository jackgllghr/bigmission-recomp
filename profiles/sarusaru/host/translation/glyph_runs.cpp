#include "translation/texture_pack.hpp"
#include "gpu/texture_decode.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <cmath>

namespace sarusaru::translation {
namespace {
bool glyph_sprite(const gpu::DrawCall &call) {
    if (!(call.through && !call.clear_mode && call.texture.enabled &&
        call.primitive == gpu::PrimitiveType::Sprites && call.vertices.size() == 2 && call.indices.empty() &&
        call.texture.width == 512 && call.texture.height == 512 && call.texture.buffer_width == 512)) return false;
    const float width = call.vertices[1].texcoord[0] - call.vertices[0].texcoord[0];
    return (width == 8 || width == 16) &&
        call.vertices[1].position[0] - call.vertices[0].position[0] == width &&
        call.vertices[1].position[1] - call.vertices[0].position[1] == 16 &&
        call.vertices[1].texcoord[1] - call.vertices[0].texcoord[1] == 16;
}
void word(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
}

void TexturePack::translate_draw(const psprecomp::GuestMemory &memory, const gpu::DrawCall &call, const DrawSink &emit) {
    if (runs_.empty() || !glyph_sprite(call)) {
        flush_draws(emit);
        emit(call);
        return;
    }
    if (!pending_.empty()) {
        const auto &first = pending_.front();
        const auto &previous = pending_.back();
        const float cell = first.vertices[1].position[0] - first.vertices[0].position[0];
        if (call.target.color_address != first.target.color_address || call.texture.address != first.texture.address ||
            call.texture.clut_address != first.texture.clut_address || call.texture.clut_format != first.texture.clut_format ||
            call.texture.clut_shift != first.texture.clut_shift || call.texture.clut_mask != first.texture.clut_mask ||
            call.texture.clut_offset != first.texture.clut_offset || call.texture.function != first.texture.function ||
            call.texture.alpha_from_texture != first.texture.alpha_from_texture ||
            call.vertices[1].color != first.vertices[1].color ||
            call.viewport.scissor_x1 != first.viewport.scissor_x1 || call.viewport.scissor_x2 != first.viewport.scissor_x2 ||
            call.viewport.scissor_y1 != first.viewport.scissor_y1 || call.viewport.scissor_y2 != first.viewport.scissor_y2 ||
            call.vertices[0].position[1] != first.vertices[0].position[1] ||
            call.vertices[1].position[0] - call.vertices[0].position[0] != cell ||
            // Both ASCII and compact text advance over undrawn space cells.
            (call.vertices[0].position[0] != previous.vertices[0].position[0] + cell &&
             call.vertices[0].position[0] != previous.vertices[0].position[0] + 2 * cell) || pending_.size() == 64)
            flush_draws(emit);
    }
    if (pending_.empty()) {
        gpu::TextureSnapshot snapshot;
        if (!gpu::snapshot_texture(memory, call.texture, snapshot)) { emit(call); return; }
        const auto &t = call.texture;
        // A sampled renderer key cannot validate a font: a single changed glyph
        // must invalidate identification, even if its address remains unchanged.
        const auto key = psprecomp::sha256(snapshot.texels) + psprecomp::sha256(snapshot.clut) + ":" +
            std::to_string(static_cast<unsigned>(t.format)) + ":" + std::to_string(t.swizzled) + ":" +
            std::to_string(t.clut_format) + ":" + std::to_string(t.clut_shift) + ":" +
            std::to_string(t.clut_mask) + ":" + std::to_string(t.clut_offset);
        auto found = fonts_.find(key);
        if (found == fonts_.end()) {
            std::vector<std::uint32_t> pixels;
            std::string hash;
            if (gpu::decode_texture(memory, call.texture, pixels)) {
                std::vector<std::uint8_t> rgba;
                rgba.reserve(pixels.size() * 4);
                for (const auto pixel : pixels) for (unsigned shift = 0; shift < 32; shift += 8)
                    rgba.push_back(static_cast<std::uint8_t>(pixel >> shift));
                const auto candidate = psprecomp::sha256(rgba);
                if (std::ranges::any_of(runs_, [&](const GlyphRun &run) { return run.font_hash == candidate; })) hash = candidate;
            }
            // Keep diagnostic cache bounded even if unrelated dynamic fonts change every frame.
            if (fonts_.size() >= 64) fonts_.clear();
            found = fonts_.emplace(key, std::move(hash)).first;
        }
        if (found->second.empty()) { emit(call); return; }
        pending_font_ = found->second;
    }
    pending_.push_back(call);
}

void TexturePack::flush_draws(const DrawSink &emit) {
    if (pending_.empty()) return;
    std::vector<std::uint8_t> signature;
    const auto &first = pending_.front();
    bool valid = true;
    for (const auto &call : pending_) {
        const float values[] = {call.vertices[0].position[0] - first.vertices[0].position[0],
                                call.vertices[0].texcoord[0], call.vertices[0].texcoord[1]};
        for (const float value : values) {
            if (!std::isfinite(value) || value < 0 || value > 1024 || value != std::round(value)) { valid = false; break; }
            word(signature, static_cast<std::uint16_t>(value));
        }
    }
    std::optional<std::size_t> matched;
    if (valid) {
        const auto found = run_index_.find(pending_font_ + psprecomp::sha256(signature));
        if (found != run_index_.end() && runs_[found->second].glyph_count == pending_.size()) matched = found->second;
    }
    // Move the pending batch before invoking a sink, so reentrant callbacks see an empty batch.
    auto original = std::move(pending_);
    pending_.clear();
    if (!matched) { for (const auto &call : original) emit(call); return; }
    const auto &run = runs_[*matched];
    auto translated = original.front();
    translated.texture.address = kHostTextureBase + static_cast<std::uint32_t>(*matched) * kHostTextureStride;
    translated.texture.width = run.image_width;
    translated.texture.height = 16;
    translated.texture.buffer_width = run.image_width;
    translated.texture.format = gpu::TextureFormat::Rgba8888;
    translated.texture.swizzled = false;
    translated.vertices[0].texcoord[0] = translated.vertices[0].texcoord[1] = 0;
    translated.vertices[1].texcoord[0] = static_cast<float>(run.width);
    translated.vertices[1].texcoord[1] = static_cast<float>(run.height);
    translated.vertices[1].position[0] = translated.vertices[0].position[0] + run.width;
    translated.vertices[1].position[1] = translated.vertices[0].position[1] + run.height;
    emit(translated);
}

bool TexturePack::host_texture(std::uint32_t address) const noexcept {
    return address >= kHostTextureBase && (address - kHostTextureBase) % kHostTextureStride == 0 &&
        (address - kHostTextureBase) / kHostTextureStride < runs_.size();
}

std::vector<std::uint8_t> TexturePack::host_rgba(std::uint32_t address) const {
    return host_image(address, 1).rgba;
}

TexturePack::HostImage TexturePack::host_image(std::uint32_t address, std::uint32_t requested_scale) const {
    if (!host_texture(address)) return {};
    const auto &run = runs_[(address - kHostTextureBase) / kHostTextureStride];
    const std::uint32_t scale = requested_scale > 1 && run.hd_scale ? run.hd_scale : 1u;
    const auto &runs = scale > 1 ? run.hd_runs : run.rgba_runs;
    HostImage image{{}, run.image_width * scale, 16u * scale};
    image.rgba.reserve(std::size_t{image.width} * image.height * 4u);
    // Validated when the pack was loaded.
    for (std::size_t at = 0; at + 6 <= runs.size(); at += 6) {
        const std::size_t count = runs[at] | std::size_t{runs[at + 1]} << 8;
        for (std::size_t i = 0; i < count; ++i) image.rgba.insert(image.rgba.end(), runs.begin() + at + 2, runs.begin() + at + 6);
    }
    return image;
}
}
