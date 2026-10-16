// The WAVE header parsing, sample-position convention and all-on-memory frame
// handling are adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision
// 39ac05f), MIT License; see profiles/sarusaru/THIRD_PARTY.md. Streaming and
// the API this game uses are written for this profile.
#include "audio/atrac.hpp"

#include "psprecomp/common.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace sarusaru::audio {
namespace {

constexpr std::uint16_t kFormatAtrac3 = 0x0270u;
constexpr std::uint16_t kFormatExtensible = 0xFFFEu;
// Samples the decoder itself delays its output by, per codec.
constexpr std::uint32_t kAtrac3DecoderDelay = 69u;
constexpr std::uint32_t kAtrac3PlusDecoderDelay = 368u;

class FfmpegFrameDecoder final : public FrameDecoder {
public:
    bool open(AtracCodec codec, unsigned channels, unsigned block_align, std::span<const std::uint8_t> extradata) override {
        return decoder_.open(codec, channels, block_align, extradata);
    }
    std::size_t decode(std::span<const std::uint8_t> frame, std::int16_t *out) override { return decoder_.decode(frame, out); }
    void reset() override { decoder_.reset(); }

private:
    AtracDecoder decoder_;
};

bool trace_enabled() {
    static const bool enabled = std::getenv("SARUSARU_TRACE_ATRAC") != nullptr;
    return enabled;
}

std::uint32_t read_le32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::uint16_t read_le16(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1u] << 8u));
}

// Parses the RIFF WAVE header at the start of the guest buffer.
std::optional<std::uint32_t> parse_header(const psprecomp::GuestMemory &memory, std::uint32_t buffer,
                                          std::uint32_t buffer_size, TrackInfo &info) {
    if (buffer_size < 12u) return atrac_error::kSizeTooSmall;
    const std::uint32_t header_bytes = std::min<std::uint32_t>(buffer_size, 0x1000u);
    if (!memory.contains(buffer, header_bytes)) return atrac_error::kParamFail;
    std::vector<std::uint8_t> bytes(header_bytes);
    memory.copy_out(buffer, bytes);
    if (read_le32(bytes, 0u) != 0x46464952u || read_le32(bytes, 8u) != 0x45564157u)  // "RIFF", "WAVE"
        return atrac_error::kUnknownFormat;
    info.file_size = read_le32(bytes, 4u) + 8u;

    bool have_format = false;
    std::optional<std::uint32_t> fact_samples;
    std::uint32_t fact_offset = 0u;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> loop;
    std::size_t offset = 12u;
    while (offset + 8u <= bytes.size()) {
        const std::uint32_t id = read_le32(bytes, offset);
        const std::uint32_t size = read_le32(bytes, offset + 4u);
        const std::size_t body = offset + 8u;
        if (id == 0x61746164u) {  // "data"
            info.data_offset = static_cast<std::uint32_t>(body);
            info.data_size = size;
            break;
        }
        if (body + size > bytes.size()) return atrac_error::kSizeTooSmall;
        if (id == 0x20746D66u && size >= 16u) {  // "fmt "
            const std::uint16_t tag = read_le16(bytes, body);
            info.channels = read_le16(bytes, body + 2u);
            info.block_align = read_le16(bytes, body + 12u);
            if (tag == kFormatAtrac3) {
                info.codec = AtracCodec::Atrac3;
                if (size >= 18u + 14u)
                    info.extradata.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body + 18u),
                                          bytes.begin() + static_cast<std::ptrdiff_t>(body + 32u));
            } else if (tag == kFormatExtensible) {
                info.codec = AtracCodec::Atrac3Plus;
            } else {
                return atrac_error::kBadCodecType;
            }
            have_format = true;
        } else if (id == 0x74636166u && size >= 4u) {  // "fact"
            fact_samples = read_le32(bytes, body);
            if (size >= 8u) fact_offset = read_le32(bytes, body + 4u);
        } else if (id == 0x6C706D73u && size >= 36u) {  // "smpl"
            const std::uint32_t loops = read_le32(bytes, body + 28u);
            if (loops > 0u && size >= 36u + 24u) loop = {read_le32(bytes, body + 36u + 8u), read_le32(bytes, body + 36u + 12u)};
        }
        offset = body + size + (size & 1u);
    }
    if (!have_format || info.data_offset == 0u) return atrac_error::kUnknownFormat;
    if (info.channels == 0u || info.channels > 2u || info.block_align == 0u) return atrac_error::kBadCodecParam;

    const auto frame_samples = static_cast<std::uint32_t>(atrac_frame_samples(info.codec));
    info.skip = fact_offset + (info.codec == AtracCodec::Atrac3 ? kAtrac3DecoderDelay : kAtrac3PlusDecoderDelay);
    const std::uint64_t decoded = static_cast<std::uint64_t>(info.data_size / info.block_align) * frame_samples;
    const std::uint64_t playable = decoded > info.skip ? decoded - info.skip : 0u;
    const std::uint64_t samples = fact_samples ? std::min<std::uint64_t>(*fact_samples, playable) : playable;
    if (samples == 0u) return atrac_error::kUnknownFormat;
    info.end_sample = static_cast<std::int32_t>(samples - 1u);
    if (loop && loop->first >= fact_offset && loop->second >= loop->first) {
        info.loop_start = static_cast<std::int32_t>(loop->first - fact_offset);
        info.loop_end = std::min(static_cast<std::int32_t>(loop->second - fact_offset), info.end_sample);
    }
    return std::nullopt;
}

} // namespace

std::unique_ptr<FrameDecoder> make_ffmpeg_decoder() {
    return std::make_unique<FfmpegFrameDecoder>();
}

AtracLibrary::Context *AtracLibrary::context(std::uint32_t id) const noexcept {
    return id < kMaxIds ? contexts_[id].get() : nullptr;
}

// Unknown and released IDs fail differently on hardware.
std::uint32_t AtracLibrary::missing(std::uint32_t id) const noexcept {
    return id < kMaxIds ? atrac_error::kNoData : atrac_error::kBadAtracId;
}

const TrackInfo *AtracLibrary::track(std::uint32_t id) const noexcept {
    const Context *found = context(id);
    return found != nullptr && found->has_data ? &found->track : nullptr;
}

bool AtracLibrary::looping(const Context &context) noexcept {
    return context.track.loop_start >= 0 && context.loop_num != 0;
}

std::int64_t AtracLibrary::frame_of(const Context &context, std::int64_t position) noexcept {
    return (position + context.track.skip) / static_cast<std::int64_t>(atrac_frame_samples(context.track.codec));
}

std::int64_t AtracLibrary::supply_end_frame(const Context &context) noexcept {
    const std::int64_t last = static_cast<std::int64_t>(context.track.data_size / context.track.block_align) - 1;
    return looping(context) ? std::min(frame_of(context, context.track.loop_end), last) : last;
}

std::uint32_t AtracLibrary::writable_bytes(const Context &context) noexcept {
    if (context.all_on_memory || context.supply_finished) return 0u;
    const std::uint64_t queued = context.queue.size() * context.track.block_align + context.pending.size();
    if (queued >= context.buffer_size) return 0u;
    const std::int64_t frames_to_wrap = supply_end_frame(context) + 1 - context.next_supply_frame;
    if (frames_to_wrap <= 0) return 0u;
    const std::uint64_t to_wrap = static_cast<std::uint64_t>(frames_to_wrap) * context.track.block_align - context.pending.size();
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(context.buffer_size - queued, to_wrap));
}

std::int32_t AtracLibrary::remain(const Context &context) noexcept {
    if (context.all_on_memory) return kRemainAllDataOnMemory;
    if (context.supply_finished) return kRemainStreamDataOnMemory;
    return static_cast<std::int32_t>(context.queue.size());
}

// Samples the next DecodeData returns: the rest of the current frame, up to
// the loop end or the track end.
std::int32_t AtracLibrary::samples_at(const Context &context) noexcept {
    const TrackInfo &track = context.track;
    if (context.position > track.end_sample) return 0;
    const bool loops = looping(context) && context.position <= track.loop_end;
    const std::int32_t stop = loops ? track.loop_end : track.end_sample;
    const auto frame_samples = static_cast<std::int64_t>(atrac_frame_samples(track.codec));
    const std::int64_t first = (static_cast<std::int64_t>(context.position) + track.skip) % frame_samples;
    return static_cast<std::int32_t>(std::min<std::int64_t>(frame_samples - first, static_cast<std::int64_t>(stop) - context.position + 1));
}

std::int32_t AtracLibrary::get_id(std::uint32_t codec_type) {
    if (codec_type != kCodecAtrac3Plus && codec_type != kCodecAtrac3) return static_cast<std::int32_t>(atrac_error::kBadCodecType);
    const auto slot = std::ranges::find_if(contexts_, [](const auto &entry) { return !entry; });
    if (slot == contexts_.end()) return static_cast<std::int32_t>(atrac_error::kNoAtracId);
    *slot = std::make_unique<Context>();
    (*slot)->requested = codec_type == kCodecAtrac3Plus ? AtracCodec::Atrac3Plus : AtracCodec::Atrac3;
    return static_cast<std::int32_t>(slot - contexts_.begin());
}

std::int32_t AtracLibrary::release(std::uint32_t id) {
    if (context(id) == nullptr) return static_cast<std::int32_t>(missing(id));
    contexts_[id].reset();
    return 0;
}

void AtracLibrary::reinit() {
    for (auto &entry : contexts_) entry.reset();
}

std::int32_t AtracLibrary::set_data(std::uint32_t id, const psprecomp::GuestMemory &memory, std::uint32_t buffer,
                                    std::uint32_t buffer_size) {
    Context *found = context(id);
    if (found == nullptr) return static_cast<std::int32_t>(missing(id));
    TrackInfo track;
    if (const auto failed = parse_header(memory, buffer, buffer_size, track)) return static_cast<std::int32_t>(*failed);
    if (track.codec != found->requested) return static_cast<std::int32_t>(atrac_error::kBadCodecType);
    if (!memory.contains(buffer, buffer_size)) return static_cast<std::int32_t>(atrac_error::kParamFail);
    auto decoder = factory_();
    if (!decoder || !decoder->open(track.codec, track.channels, track.block_align, track.extradata))
        return static_cast<std::int32_t>(atrac_error::kBadCodecParam);
    const AtracCodec requested = found->requested;
    *found = Context{};
    found->requested = requested;
    found->has_data = true;
    found->track = std::move(track);
    found->buffer = buffer;
    found->buffer_size = buffer_size;
    found->decoder = std::move(decoder);
    found->all_on_memory = buffer_size >= found->track.file_size;
    if (!found->all_on_memory) {
        // The data bytes already in the buffer are the start of the stream.
        const std::uint32_t available = buffer_size > found->track.data_offset ? buffer_size - found->track.data_offset : 0u;
        std::vector<std::uint8_t> initial(available);
        memory.copy_out(buffer + found->track.data_offset, initial);
        feed(*found, initial);
    }
    if (trace_enabled())
        std::cerr << "[atrac] SetData id=" << id << " file=" << found->track.file_size << " buffer=" << buffer_size
                  << (found->all_on_memory ? " (all on memory)" : " (streamed)") << " align=" << found->track.block_align
                  << " end=" << found->track.end_sample << " loop=" << found->track.loop_start << ".."
                  << found->track.loop_end << "\n";
    return 0;
}

// Moves supplied stream bytes into the frame queue. At the end of the supply
// segment the supply wraps to the loop start (or finishes); the rest of the
// bytes, which lie beyond it, are not part of playback and are dropped.
void AtracLibrary::feed(Context &context, std::span<const std::uint8_t> bytes) {
    const std::uint32_t align = context.track.block_align;
    for (const std::uint8_t byte : bytes) {
        if (context.supply_finished) return;
        context.pending.push_back(byte);
        if (context.pending.size() < align) continue;
        const std::int64_t frame = context.next_supply_frame;
        context.queue.emplace_back(frame, std::move(context.pending));
        context.pending.clear();
        if (frame == supply_end_frame(context)) {
            if (looping(context)) {
                context.next_supply_frame = frame_of(context, context.track.loop_start);
            } else {
                context.supply_finished = true;
            }
            return;
        }
        context.next_supply_frame = frame + 1;
    }
}

std::int32_t AtracLibrary::set_loop_num(std::uint32_t id, std::int32_t loops) {
    Context *found = context(id);
    if (found == nullptr || !found->has_data) return static_cast<std::int32_t>(found == nullptr ? missing(id) : atrac_error::kNoData);
    if (found->track.loop_start < 0) return static_cast<std::int32_t>(atrac_error::kNoLoopInformation);
    found->loop_num = loops;
    return 0;
}

std::int32_t AtracLibrary::stream_info(std::uint32_t id, StreamInfo &info) const {
    const Context *found = context(id);
    if (found == nullptr || !found->has_data) return static_cast<std::int32_t>(found == nullptr ? missing(id) : atrac_error::kNoData);
    info.write_address = found->buffer;
    info.writable = writable_bytes(*found);
    info.read_offset = found->all_on_memory ? found->track.file_size
        : found->track.data_offset + static_cast<std::uint32_t>(found->next_supply_frame) * found->track.block_align +
              static_cast<std::uint32_t>(found->pending.size());
    return 0;
}

std::int32_t AtracLibrary::add_stream_data(std::uint32_t id, const psprecomp::GuestMemory &memory, std::uint32_t bytes) {
    Context *found = context(id);
    if (found == nullptr || !found->has_data) return static_cast<std::int32_t>(found == nullptr ? missing(id) : atrac_error::kNoData);
    if (bytes > writable_bytes(*found)) return static_cast<std::int32_t>(atrac_error::kParamFail);
    std::vector<std::uint8_t> data(bytes);
    if (bytes != 0u) memory.copy_out(found->buffer, data);
    feed(*found, data);
    return 0;
}

// Decodes `frame` into the PCM cache.
bool AtracLibrary::load_frame(const psprecomp::GuestMemory &memory, Context &context, std::int64_t frame) {
    if (frame == context.pcm_frame) return true;
    const TrackInfo &track = context.track;
    context.pcm.assign(atrac_frame_samples(track.codec) * 2u, 0);
    const auto decode = [&](std::int64_t index, std::span<const std::uint8_t> bytes) {
        if (index != context.next_decoder_frame) { context.decoder->reset(); context.decoder_history.clear(); context.history_bytes=0; context.history_complete=true; }
        context.next_decoder_frame = index + 1;
        if (context.history_complete) {
            if (bytes.size()>8u*1024u*1024u-context.history_bytes || context.decoder_history.size()>=65536) {
                context.decoder_history.clear(); context.history_bytes=0; context.history_complete=false;
            } else {
                context.history_bytes+=bytes.size(); context.decoder_history.emplace_back(bytes.begin(),bytes.end());
            }
        }
        return context.decoder->decode(bytes, context.pcm.data()) != 0u;
    };
    bool decoded = false;
    if (context.all_on_memory) {
        std::vector<std::uint8_t> bytes(track.block_align);
        const auto read = [&](std::int64_t index) {
            const std::uint64_t offset = track.data_offset + static_cast<std::uint64_t>(index) * track.block_align;
            if (offset + track.block_align > std::min<std::uint64_t>(context.buffer_size, track.data_offset + track.data_size))
                return false;
            memory.copy_out(context.buffer + static_cast<std::uint32_t>(offset), bytes);
            return true;
        };
        if (frame != context.next_decoder_frame) {
            // A frame's output depends on the ones before it: after a seek, two
            // frames of warm-up make the output identical to decoding straight through.
            context.decoder->reset();
            context.decoder_history.clear(); context.history_bytes=0; context.history_complete=true;
            for (std::int64_t warm_up = std::max<std::int64_t>(frame - 2, 0); warm_up < frame; ++warm_up)
                if (read(warm_up)) (void)decode(warm_up, bytes);
            context.next_decoder_frame = frame;
        }
        decoded = read(frame) && decode(frame, bytes);
    } else {
        while (!context.queue.empty() && context.queue.front().first != frame) context.queue.pop_front();
        if (context.queue.empty()) return false;
        decoded = decode(frame, context.queue.front().second);
        context.queue.pop_front();
    }
    if (!decoded) {
        std::fill(context.pcm.begin(), context.pcm.end(), std::int16_t{0});
        if (trace_enabled()) std::cerr << "[atrac] frame " << frame << " failed to decode; silence\n";
    } else if (trace_enabled() && frame % 500 == 0) {
        std::cerr << "[atrac] decoded frame " << frame << " (first sample " << context.pcm[0] << ")\n";
    }
    context.pcm_frame = frame;
    return true;
}

std::int32_t AtracLibrary::decode(std::uint32_t id, psprecomp::GuestMemory &memory, std::uint32_t output, Decoded &decoded) {
    Context *found = context(id);
    if (found == nullptr || !found->has_data) return static_cast<std::int32_t>(found == nullptr ? missing(id) : atrac_error::kNoData);
    Context &context = *found;
    const TrackInfo &track = context.track;
    decoded = {0, false, remain(context)};
    if (context.position > track.end_sample) {
        decoded.end = true;
        return static_cast<std::int32_t>(atrac_error::kAllDataDecoded);
    }
    const bool loops = looping(context) && context.position <= track.loop_end;
    const std::int32_t stop = loops ? track.loop_end : track.end_sample;
    const auto frame_samples = static_cast<std::int64_t>(atrac_frame_samples(track.codec));
    const std::int64_t first = (static_cast<std::int64_t>(context.position) + track.skip) % frame_samples;
    const std::int32_t count = samples_at(context);
    if (!load_frame(memory, context, frame_of(context, context.position)))
        return static_cast<std::int32_t>(atrac_error::kBufferIsEmpty);
    if (output != 0u) {
        if (!memory.contains(output, static_cast<std::size_t>(count) * 4u)) return static_cast<std::int32_t>(atrac_error::kParamFail);
        const std::int16_t *source = context.pcm.data() + first * 2;
        for (std::int32_t i = 0; i < count * 2; ++i)
            memory.store16(output + static_cast<std::uint32_t>(i) * 2u, static_cast<std::uint16_t>(source[i]));
    }
    context.position += count;
    if (context.position > stop && loops) {
        context.position = track.loop_start;
        if (context.loop_num > 0) --context.loop_num;
    }
    decoded = {count, context.position > track.end_sample, remain(context)};
    return 0;
}

std::int32_t AtracLibrary::remain_frames(std::uint32_t id, std::int32_t &frames) const {
    const Context *found = context(id);
    if (found == nullptr || !found->has_data) return static_cast<std::int32_t>(found == nullptr ? missing(id) : atrac_error::kNoData);
    frames = remain(*found);
    return 0;
}

std::int32_t AtracLibrary::next_sample(std::uint32_t id, std::int32_t &samples) const {
    const Context *found = context(id);
    if (found == nullptr || !found->has_data) return static_cast<std::int32_t>(found == nullptr ? missing(id) : atrac_error::kNoData);
    samples = samples_at(*found);
    return 0;
}

void register_atrac(AtracLibrary &library, psprecomp::Runtime &runtime) {
    const auto store = [](psprecomp::GuestMemory &memory, std::uint32_t address, std::int32_t value) {
        if (address != 0u && memory.contains(address, 4u)) memory.store32(address, static_cast<std::uint32_t>(value));
    };
    const auto set = [](psprecomp::AllegrexContext &ctx, std::int32_t result) { ctx.set_gpr(2u, static_cast<std::uint32_t>(result)); };
    runtime.register_hle("sceAtrac3plus", 0x780F88D1u,  // sceAtracGetAtracID(codecType)
        [&library, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, library.get_id(ctx.gpr[4])); });
    runtime.register_hle("sceAtrac3plus", 0x61EB33F5u,  // sceAtracReleaseAtracID(id)
        [&library, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { set(ctx, library.release(ctx.gpr[4])); });
    runtime.register_hle("sceAtrac3plus", 0x132F1ECAu,  // sceAtracReinit(atrac3Count, atrac3plusCount)
        [&library](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) { library.reinit(); ctx.set_gpr(2u, 0u); });
    runtime.register_hle("sceAtrac3plus", 0x0E2A73ABu,  // sceAtracSetData(id, buffer, bufferSize)
        [&library, set](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            set(ctx, library.set_data(ctx.gpr[4], rt.memory(), ctx.gpr[5], ctx.gpr[6]));
        });
    runtime.register_hle("sceAtrac3plus", 0x868120B5u,  // sceAtracSetLoopNum(id, loops)
        [&library, set](psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
            set(ctx, library.set_loop_num(ctx.gpr[4], static_cast<std::int32_t>(ctx.gpr[5])));
        });
    runtime.register_hle("sceAtrac3plus", 0x5D268707u,  // sceAtracGetStreamDataInfo(id, *writePtr, *writable, *readOffset)
        [&library, set, store](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            AtracLibrary::StreamInfo info;
            const auto result = library.stream_info(ctx.gpr[4], info);
            if (result == 0) {
                store(rt.memory(), ctx.gpr[5], static_cast<std::int32_t>(info.write_address));
                store(rt.memory(), ctx.gpr[6], static_cast<std::int32_t>(info.writable));
                store(rt.memory(), ctx.gpr[7], static_cast<std::int32_t>(info.read_offset));
            }
            set(ctx, result);
        });
    runtime.register_hle("sceAtrac3plus", 0x7DB31251u,  // sceAtracAddStreamData(id, bytes)
        [&library, set](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            set(ctx, library.add_stream_data(ctx.gpr[4], rt.memory(), ctx.gpr[5]));
        });
    runtime.register_hle("sceAtrac3plus", 0x6A8C3CD5u,  // sceAtracDecodeData(id, out, *samples, *end, *remain in t0)
        [&library, set, store](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            AtracLibrary::Decoded decoded;
            const auto result = library.decode(ctx.gpr[4], rt.memory(), ctx.gpr[5], decoded);
            store(rt.memory(), ctx.gpr[6], decoded.samples);
            store(rt.memory(), ctx.gpr[7], decoded.end ? 1 : 0);
            store(rt.memory(), ctx.gpr[8], decoded.remain);
            set(ctx, result);
        });
    runtime.register_hle("sceAtrac3plus", 0x9AE849A7u,  // sceAtracGetRemainFrame(id, *remain)
        [&library, set, store](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            std::int32_t frames = 0;
            const auto result = library.remain_frames(ctx.gpr[4], frames);
            if (result == 0) store(rt.memory(), ctx.gpr[5], frames);
            set(ctx, result);
        });
    runtime.register_hle("sceAtrac3plus", 0x36FAABFBu,  // sceAtracGetNextSample(id, *samples)
        [&library, set, store](psprecomp::Runtime &rt, psprecomp::AllegrexContext &ctx) {
            std::int32_t samples = 0;
            const auto result = library.next_sample(ctx.gpr[4], samples);
            if (result == 0) store(rt.memory(), ctx.gpr[5], samples);
            set(ctx, result);
        });
}

} // namespace sarusaru::audio
