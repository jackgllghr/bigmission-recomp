#include "movie/psmf_player.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <span>

namespace sarusaru::movie {
namespace {

constexpr std::uint32_t kConfigLoop = 0u;
constexpr std::uint32_t kConfigPixelType = 1u;
constexpr std::uint8_t kVideoStreamId = 0xE0u;
constexpr std::uint8_t kAudioStreamId = 0xBDu;

std::uint32_t be32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24u) | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u) | bytes[offset + 3u];
}

// A 6-byte PSMF timestamp; the top 16 bits are zero in practice.
std::uint32_t timestamp(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return be32(bytes, offset + 2u);
}

std::uint8_t clamp8(std::int32_t value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

bool trace_enabled() {
    static const bool enabled = std::getenv("SARUSARU_TRACE_MOVIE") != nullptr;
    return enabled;
}

} // namespace

std::optional<PsmfInfo> parse_psmf(const std::vector<std::uint8_t> &file) {
    if (file.size() < 0x82u || file[0] != 'P' || file[1] != 'S' || file[2] != 'M' || file[3] != 'F') return std::nullopt;
    PsmfInfo info;
    info.stream_offset = be32(file, 8u);
    info.stream_size = be32(file, 12u);
    info.start_pts = timestamp(file, 0x54u);
    info.end_pts = timestamp(file, 0x5Au);
    if (info.stream_offset > file.size() || info.end_pts < info.start_pts) return std::nullopt;
    const std::uint32_t streams = (static_cast<std::uint32_t>(file[0x80u]) << 8u) | file[0x81u];
    for (std::uint32_t i = 0; i < streams && 0x82u + (i + 1u) * 16u <= file.size(); ++i) {
        const std::uint8_t id = file[0x82u + i * 16u];
        if ((id & 0xF0u) == kVideoStreamId) ++info.video_streams;
        else if (id == kAudioStreamId) ++info.audio_streams;
    }
    return info;
}

void convert_picture(const Picture &picture, PixelType type, std::uint32_t stride, std::uint8_t *out) {
    const std::uint32_t chroma_width = (picture.width + 1u) / 2u;
    const std::uint32_t bytes_per_pixel = type == PixelType::Rgba8888 ? 4u : 2u;
    for (std::uint32_t row = 0; row < picture.height; ++row) {
        std::uint8_t *line = out + static_cast<std::size_t>(row) * stride * bytes_per_pixel;
        for (std::uint32_t column = 0; column < picture.width; ++column) {
            const std::size_t chroma = static_cast<std::size_t>(row / 2u) * chroma_width + column / 2u;
            const std::int32_t cb = picture.cb[chroma] - 128;
            const std::int32_t cr = picture.cr[chroma] - 128;
            std::int32_t r{}, g{}, b{};
            const std::int32_t y = picture.y[static_cast<std::size_t>(row) * picture.width + column];
            if (picture.full_range) {
                r = y + ((359 * cr) >> 8);
                g = y - ((88 * cb + 183 * cr) >> 8);
                b = y + ((454 * cb) >> 8);
            } else {  // BT.601 limited range
                const std::int32_t luma = 298 * (y - 16);
                r = (luma + 409 * cr + 128) >> 8;
                g = (luma - 100 * cb - 208 * cr + 128) >> 8;
                b = (luma + 516 * cb + 128) >> 8;
            }
            const std::uint8_t r8 = clamp8(r), g8 = clamp8(g), b8 = clamp8(b);
            std::uint8_t *pixel = line + static_cast<std::size_t>(column) * bytes_per_pixel;
            std::uint16_t packed = 0u;
            switch (type) {
            case PixelType::Rgba8888:
                pixel[0] = r8; pixel[1] = g8; pixel[2] = b8; pixel[3] = 0xFFu;
                continue;
            case PixelType::Rgb565:
                packed = static_cast<std::uint16_t>((r8 >> 3u) | ((g8 >> 2u) << 5u) | ((b8 >> 3u) << 11u));
                break;
            case PixelType::Rgba5551:
                packed = static_cast<std::uint16_t>((r8 >> 3u) | ((g8 >> 3u) << 5u) | ((b8 >> 3u) << 10u) | 0x8000u);
                break;
            case PixelType::Rgba4444:
                packed = static_cast<std::uint16_t>((r8 >> 4u) | ((g8 >> 4u) << 4u) | ((b8 >> 4u) << 8u) | 0xF000u);
                break;
            }
            pixel[0] = static_cast<std::uint8_t>(packed);
            pixel[1] = static_cast<std::uint8_t>(packed >> 8u);
        }
    }
}

PsmfPlayer::Player *PsmfPlayer::find(std::uint32_t handle) noexcept {
    const auto found = players_.find(handle);
    return found == players_.end() ? nullptr : &found->second;
}

const PsmfPlayer::Player *PsmfPlayer::find(std::uint32_t handle) const noexcept {
    const auto found = players_.find(handle);
    return found == players_.end() ? nullptr : &found->second;
}

std::int32_t PsmfPlayer::create(std::uint32_t handle) {
    players_[handle] = Player{};
    return 0;
}

std::int32_t PsmfPlayer::remove(std::uint32_t handle) {
    return players_.erase(handle) != 0u ? 0 : static_cast<std::int32_t>(psmf_error::kInvalidStatus);
}

std::int32_t PsmfPlayer::set_psmf(std::uint32_t handle, const std::string &path) {
    Player *player = find(handle);
    if (player == nullptr || player->status != psmf_status::kInit) return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    auto file = reader_(path);
    if (!file) return static_cast<std::int32_t>(psmf_error::kInvalidParam);
    const auto info = parse_psmf(*file);
    if (!info) return static_cast<std::int32_t>(psmf_error::kInvalidStream);
    player->file = std::move(*file);
    player->info = *info;
    player->status = psmf_status::kStandby;
    if (trace_enabled())
        std::cerr << "[movie] " << path << ": " << player->file.size() << " bytes, " << info->video_streams << " video, "
                  << info->audio_streams << " audio, " << (info->end_pts - info->start_pts) << " ticks\n";
    return 0;
}

std::int32_t PsmfPlayer::release_psmf(std::uint32_t handle) {
    Player *player = find(handle);
    if (player == nullptr || player->status != psmf_status::kStandby) return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    const PixelType pixel_type = player->pixel_type;
    *player = Player{};
    player->pixel_type = pixel_type;
    return 0;
}

std::int32_t PsmfPlayer::info(std::uint32_t handle, PsmfInfo &info) const {
    const Player *player = find(handle);
    if (player == nullptr || player->status < psmf_status::kStandby) return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    info = player->info;
    return 0;
}

std::int32_t PsmfPlayer::configure(std::uint32_t handle, std::uint32_t mode, std::int32_t value) {
    Player *player = find(handle);
    if (player == nullptr) return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    if (mode == kConfigLoop && (value == 0 || value == 1)) {
        player->loop = value == 1;
        return 0;
    }
    if (mode == kConfigPixelType && value >= -1 && value <= 3) {
        player->pixel_type = value == -1 ? PixelType::Rgba8888 : static_cast<PixelType>(value);
        return 0;
    }
    return static_cast<std::int32_t>(psmf_error::kInvalidConfig);
}

void PsmfPlayer::restart(Player &player) {
    player.demuxer.reset();
    player.next_pack = 0u;
    player.video = std::make_unique<AvcDecoder>();
    if (!player.video->open()) player.video.reset();
    player.audio.reset();
    player.picture.reset();
    player.pictures_out = 0u;
    player.picture_pts = player.info.start_pts;
    player.video_ended = player.video == nullptr;
    player.start_us = kernel_->now_us();
}

std::int32_t PsmfPlayer::start(psprecomp::Runtime &runtime, std::uint32_t handle, std::uint32_t initial_pts) {
    Player *player = find(handle);
    if (player == nullptr || player->status != psmf_status::kStandby) return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    if (initial_pts != 0u) {
        runtime.stop("scePsmfPlayerStart from a position other than 0 is not modeled yet");
        return static_cast<std::int32_t>(psmf_error::kInvalidParam);
    }
    restart(*player);
    player->status = psmf_status::kPlaying;
    return 0;
}

std::int32_t PsmfPlayer::stop(std::uint32_t handle) {
    Player *player = find(handle);
    if (player == nullptr || (player->status != psmf_status::kPlaying && player->status != psmf_status::kFinished))
        return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    player->status = psmf_status::kStandby;
    return 0;
}

std::int32_t PsmfPlayer::status(std::uint32_t handle) const {
    const Player *player = find(handle);
    return player == nullptr ? static_cast<std::int32_t>(psmf_error::kInvalidStatus) : static_cast<std::int32_t>(player->status);
}

void PsmfPlayer::fill(Player &player, bool want_video) {
    const std::size_t end = std::min<std::size_t>(player.file.size(),
                                                  static_cast<std::size_t>(player.info.stream_offset) + player.info.stream_size);
    while (!(want_video ? player.demuxer.video_ready() : player.demuxer.audio_ready())) {
        const std::size_t offset = player.info.stream_offset + player.next_pack * kPackSize;
        if (offset + kPackSize > end) {
            player.demuxer.end_of_stream();
            return;
        }
        (void)player.demuxer.push_pack(std::span(player.file).subspan(offset, kPackSize));
        ++player.next_pack;
    }
}

bool PsmfPlayer::next_picture(Player &player) {
    if (player.video_ended) return false;
    Picture picture;
    for (;;) {
        fill(player, true);
        const auto unit = player.demuxer.pop_video();
        if (!unit) break;
        if (player.video->decode(unit->data, picture)) {
            player.picture = std::move(picture);
            player.picture_pts = player.info.start_pts + static_cast<std::uint32_t>(player.pictures_out * kVideoFrameTicks);
            ++player.pictures_out;
            return true;
        }
    }
    if (player.video->drain(picture)) {
        player.picture = std::move(picture);
        player.picture_pts = player.info.start_pts + static_cast<std::uint32_t>(player.pictures_out * kVideoFrameTicks);
        ++player.pictures_out;
        return true;
    }
    player.video_ended = true;
    return false;
}

std::int32_t PsmfPlayer::update(std::uint32_t handle) {
    Player *player = find(handle);
    if (player == nullptr || (player->status != psmf_status::kPlaying && player->status != psmf_status::kFinished))
        return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    if (player->status != psmf_status::kPlaying) return 0;
    const std::uint64_t elapsed = (kernel_->now_us() - player->start_us) * 90'000u / 1'000'000u;
    const std::uint64_t length = player->info.end_pts - player->info.start_pts;
    // Finished once the stream's length (from its header) has played.
    if (elapsed >= length) {
        if (player->loop) {
            restart(*player);
        } else {
            player->status = psmf_status::kFinished;
            if (trace_enabled()) std::cerr << "[movie] finished after " << player->pictures_out << " pictures\n";
        }
    }
    return 0;
}

std::int32_t PsmfPlayer::video(psprecomp::GuestMemory &memory, std::uint32_t handle, std::uint32_t frame_width,
                               std::uint32_t buffer, std::uint32_t &pts) {
    Player *player = find(handle);
    if (player == nullptr || (player->status != psmf_status::kPlaying && player->status != psmf_status::kFinished))
        return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    const std::uint64_t target = player->info.start_pts + (kernel_->now_us() - player->start_us) * 90'000u / 1'000'000u;
    if (!player->picture) (void)next_picture(*player);
    while (!player->video_ended && player->info.start_pts + player->pictures_out * kVideoFrameTicks <= target) {
        if (!next_picture(*player)) break;
    }
    if (!player->picture) return static_cast<std::int32_t>(psmf_error::kNoMoreData);
    const Picture &picture = *player->picture;
    const std::uint32_t bytes_per_pixel = player->pixel_type == PixelType::Rgba8888 ? 4u : 2u;
    if (frame_width < picture.width) return static_cast<std::int32_t>(psmf_error::kInvalidParam);
    const std::size_t size = static_cast<std::size_t>(frame_width) * picture.height * bytes_per_pixel;
    std::uint8_t *destination = memory.raw_pointer(buffer, size);
    if (destination == nullptr) return static_cast<std::int32_t>(psmf_error::kInvalidParam);
    convert_picture(picture, player->pixel_type, frame_width, destination);
    pts = player->picture_pts;
    if (trace_enabled() && player->pictures_out % 300u == 1u)
        std::cerr << "[movie] picture " << player->pictures_out << " " << picture.width << "x" << picture.height
                  << " pts " << pts << " centre luma " << int(picture.y[picture.y.size() / 2u]) << "\n";
    return 0;
}

std::int32_t PsmfPlayer::audio(psprecomp::GuestMemory &memory, std::uint32_t handle, std::uint32_t buffer) {
    Player *player = find(handle);
    if (player == nullptr || (player->status != psmf_status::kPlaying && player->status != psmf_status::kFinished))
        return static_cast<std::int32_t>(psmf_error::kInvalidStatus);
    constexpr std::size_t kBytes = kAtracFrameSamples * 4u;
    if (!memory.contains(buffer, kBytes)) return static_cast<std::int32_t>(psmf_error::kInvalidParam);
    std::vector<std::int16_t> samples(kAtracFrameSamples * 2u, 0);
    fill(*player, false);
    const auto unit = player->demuxer.pop_audio();
    if (unit) {
        if (!player->audio) {
            player->audio = std::make_unique<audio::AtracDecoder>();
            if (!player->audio->open(audio::AtracCodec::Atrac3Plus, std::max(1u, player->demuxer.audio_channels()),
                                     static_cast<unsigned>(unit->data.size()), {}))
                player->audio.reset();
        }
        if (player->audio) (void)player->audio->decode(unit->data, samples.data());
    }
    for (std::size_t i = 0; i < samples.size(); ++i)
        memory.store16(buffer + static_cast<std::uint32_t>(i) * 2u, static_cast<std::uint16_t>(samples[i]));
    return unit || !player->video_ended ? 0 : static_cast<std::int32_t>(psmf_error::kNoMoreData);
}

void register_psmf_player(PsmfPlayer &player, psprecomp::Runtime &runtime) {
    // SARUSARU_TRACE_MOVIE=1 logs every player call with its arguments and result.
    const auto set = [](psprecomp::AllegrexContext &ctx, std::int32_t result, const char *name = nullptr) {
        if (name != nullptr && trace_enabled())
            std::cerr << "[movie] " << name << "(" << psprecomp::hex32(ctx.gpr[4]) << ", " << psprecomp::hex32(ctx.gpr[5])
                      << ", " << psprecomp::hex32(ctx.gpr[6]) << ") -> " << psprecomp::hex32(static_cast<std::uint32_t>(result)) << "\n";
        ctx.set_gpr(2u, static_cast<std::uint32_t>(result));
    };
    runtime.register_hle("sceMpeg", 0x682A619Bu,  // sceMpegInit
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 0u); });
    runtime.register_hle("sceMpeg", 0x874624D6u,  // sceMpegFinish
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { ctx.set_gpr(2u, 0u); });
    runtime.register_hle("scePsmfPlayer", 0x235D8787u,  // scePsmfPlayerCreate(player, createData)
        [&player, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, player.create(ctx.gpr[4]), "Create"); });
    runtime.register_hle("scePsmfPlayer", 0x9B71A274u,  // scePsmfPlayerDelete(player)
        [&player, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, player.remove(ctx.gpr[4]), "Delete"); });
    runtime.register_hle("scePsmfPlayer", 0x3D6D25A9u,  // scePsmfPlayerSetPsmf(player, path)
        [&player, set](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            std::string path;
            try { path = rt.memory().read_c_string(ctx.gpr[5], 256u); }
            catch (const psprecomp::Error &) { set(ctx, static_cast<std::int32_t>(psmf_error::kInvalidParam), "SetPsmf"); return; }
            set(ctx, player.set_psmf(ctx.gpr[4], path), "SetPsmf");
        });
    runtime.register_hle("scePsmfPlayer", 0xE792CD94u,  // scePsmfPlayerReleasePsmf(player)
        [&player, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, player.release_psmf(ctx.gpr[4]), "ReleasePsmf"); });
    runtime.register_hle("scePsmfPlayer", 0xDF089680u,  // scePsmfPlayerGetPsmfInfo(player, info)
        [&player, set](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            PsmfInfo info;
            const auto result = player.info(ctx.gpr[4], info);
            if (result == 0) {
                if (!rt.memory().contains(ctx.gpr[5], 20u)) { set(ctx, static_cast<std::int32_t>(psmf_error::kInvalidParam), "GetPsmfInfo"); return; }
                rt.memory().store32(ctx.gpr[5], info.end_pts - info.start_pts);
                rt.memory().store32(ctx.gpr[5] + 4u, info.video_streams);
                rt.memory().store32(ctx.gpr[5] + 8u, info.audio_streams);
                rt.memory().store32(ctx.gpr[5] + 12u, 0u);  // PCM streams
                rt.memory().store32(ctx.gpr[5] + 16u, 0u);  // player version
            }
            set(ctx, result, "GetPsmfInfo");
        });
    runtime.register_hle("scePsmfPlayer", 0x1E57A8E7u,  // scePsmfPlayerConfigPlayer(player, mode, value)
        [&player, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            set(ctx, player.configure(ctx.gpr[4], ctx.gpr[5], static_cast<std::int32_t>(ctx.gpr[6])), "ConfigPlayer");
        });
    runtime.register_hle("scePsmfPlayer", 0x95A84EE5u,  // scePsmfPlayerStart(player, params, initPts)
        [&player, set](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) { set(ctx, player.start(rt, ctx.gpr[4], ctx.gpr[6]), "Start"); });
    runtime.register_hle("scePsmfPlayer", 0x1078C008u,  // scePsmfPlayerStop(player)
        [&player, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, player.stop(ctx.gpr[4]), "Stop"); });
    runtime.register_hle("scePsmfPlayer", 0xA0B8CA55u,  // scePsmfPlayerUpdate(player)
        [&player, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, player.update(ctx.gpr[4]), "Update"); });
    runtime.register_hle("scePsmfPlayer", 0xF8EF08A6u,  // scePsmfPlayerGetCurrentStatus(player)
        [&player, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, player.status(ctx.gpr[4]), "GetCurrentStatus"); });
    runtime.register_hle("scePsmfPlayer", 0x46F61F8Bu,  // scePsmfPlayerGetVideoData(player, {frameWidth, buf, pts})
        [&player, set](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            const std::uint32_t data = ctx.gpr[5];
            if (!rt.memory().contains(data, 12u)) { set(ctx, static_cast<std::int32_t>(psmf_error::kInvalidParam), "GetVideoData"); return; }
            std::uint32_t pts = 0u;
            const auto result = player.video(rt.memory(), ctx.gpr[4], rt.memory().load32(data), rt.memory().load32(data + 4u), pts);
            if (result == 0) rt.memory().store32(data + 8u, pts);
            set(ctx, result, "GetVideoData");
        });
    runtime.register_hle("scePsmfPlayer", 0xB9848A74u,  // scePsmfPlayerGetAudioData(player, buffer)
        [&player, set](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            set(ctx, player.audio(rt.memory(), ctx.gpr[4], ctx.gpr[5]), "GetAudioData");
        });
}

} // namespace sarusaru::movie
