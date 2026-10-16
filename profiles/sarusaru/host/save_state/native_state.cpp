#include "save_state/native_state.hpp"
#include "save_state/native_archive.hpp"
#include "save_state/kernel_snapshot.hpp"
#include "session.hpp"
#include "game_abi.hpp"
#include "native_state_abi.hpp"
#include "psprecomp/sha256.hpp"
#include <cstring>
#include <tuple>

namespace sarusaru::save_state {
struct SessionCodec {
    template<class A> static void visit(A &a, StartupHle &v) { a(v.compiled_sdk_version_, v.compiler_version_, v.gpo_, v.power_ticks_); }
    template<class A> static void visit(A &a, umd::UmdDrive &v) { a(v.activated_); }
    template<class A> static void visit(A &a, io::IoManager &v) { a(v.files_); }
    template<class A> static void visit(A &a, io::IoManager::File &v) { a(v.directory, v.open_failed, v.entry, v.position, v.listing, v.next_entry, v.async_result, v.close_after_collect); }
    template<class A> static void visit(A &a, umd::DiscImage::Entry &v) { a(v.name, v.sector, v.size, v.directory); }
    template<class A> static void visit(A &a, ctrl::Controller &v) { a(v.mode_, v.cycle_, v.idle_reset_, v.idle_back_, v.pad_, v.consumed_buttons_, v.consumed_since_us_, v.script_, v.last_read_sample_); }
    template<class A> static void visit(A &a, ctrl::Controller::Pad &v) { a(v.buttons, v.lx, v.ly); }
    template<class A> static void visit(A &a, ctrl::Controller::ScriptEvent &v) { a(v.time_us, v.pad); }
    template<class A> static void visit(A &a, display::Display &v) { a(v.mode_, v.width_, v.height_, v.latched_, v.requested_, v.latch_at_vblank_, v.requests_, v.latches_); }
    template<class A> static void visit(A &a, display::Display::Framebuffer &v) { a(v.address, v.buffer_width, v.pixel_format); }
    template<class A> static void visit(A &a, game::FrameRateOverride &v) { a(v.interval_, v.overridden_frames_, v.waiting_object_, v.previous_vcount_, v.sampled_vcount_); }
    template<class A> static void visit(A &a, game::GameplayCamera &v) { a(v.seen_, v.last_frame_, v.calls_); }
    template<class A> static void visit(A &a, game::IntroLogos &v) { a(v.requested_, v.finished_); }
    template<class A> static void visit(A &a, game::OrbitCamera &v) { a(v.input_, v.yaw_, v.pitch_, v.object_, v.source_, v.last_update_us_, v.adjusted_frames_, v.seen_); }
    template<class A> static void visit(A &a, game::OrbitCamera::Input &v) { a(v.x, v.y, v.reset); }
    template<class A> static void visit(A &a, ge::Ge &v) { a(v.callbacks_, v.lists_, v.queue_, v.draw_waiters_, v.registers_, v.base_, v.state_, v.stats_); }
    template<class A> static void visit(A &a, ge::Ge::Callback &v) { a(v.signal_function, v.signal_argument, v.finish_function, v.finish_argument, v.gp); }
    template<class A> static void visit(A &a, ge::Ge::CallFrame &v) { a(v.return_address, v.offset); }
    template<class A> static void visit(A &a, ge::Ge::List &v) { a(v.used, v.in_use, v.state, v.pc, v.stall, v.offset, v.callback, v.stack, v.waiters); }
    template<class A> static void visit(A &a, ge::Ge::Stats &v) { a(v.commands, v.primitives, v.lists_completed); }
    template<class A> static void visit(A &a, gpu::GeState &v) { a(v.registers_, v.target_, v.texture_, v.blend_, v.depth_, v.alpha_test_, v.viewport_, v.culling_enabled_, v.cull_clockwise_, v.clear_mode_, v.clear_flags_, v.material_color_, v.lighting_enabled_, v.lighting_, v.fog_, v.environment_version_, v.material_version_, v.vertex_type_, v.vertex_address_, v.index_address_, v.base_extended_, v.offset_address_, v.world_, v.view_, v.projection_, v.texture_matrix_, v.bone_matrices_, v.world_write_index_, v.view_write_index_, v.view_matrix_source_, v.projection_write_index_, v.texture_write_index_, v.bone_write_index_, v.call_stack_, v.command_pc_, v.draw_count_, v.vertex_count_, v.unhandled_commands_); }
    template<class A> static void visit(A &a, gpu::TextureState &v) { a(v.enabled, v.address, v.buffer_width, v.width, v.height, v.format, v.swizzled, v.clut_address, v.clut_format, v.clut_shift, v.clut_mask, v.clut_offset, v.clut_format_word, v.clut_load_bytes, v.clut_max_bytes, v.function, v.alpha_from_texture, v.min_filter, v.mag_filter, v.wrap_s, v.wrap_t, v.scale_u, v.scale_v, v.offset_u, v.offset_v); }
    template<class A> static void visit(A &a, gpu::RenderTarget &v) { a(v.color_address, v.color_stride, v.color_format, v.depth_address, v.depth_stride); }
    template<class A> static void visit(A &a, gpu::BlendState &v) { a(v.enabled, v.source_factor, v.destination_factor, v.equation, v.fixed_source, v.fixed_destination); }
    template<class A> static void visit(A &a, gpu::DepthState &v) { a(v.test_enabled, v.write_enabled, v.function, v.range_near, v.range_far); }
    template<class A> static void visit(A &a, gpu::AlphaTestState &v) { a(v.enabled, v.function, v.reference, v.mask); }
    template<class A> static void visit(A &a, gpu::ViewportState &v) { a(v.x_scale, v.y_scale, v.z_scale, v.x_offset, v.y_offset, v.z_offset, v.scissor_x1, v.scissor_y1, v.scissor_x2, v.scissor_y2, v.offset_x, v.offset_y); }
    template<class A> static void visit(A &a, gpu::LightState &v) { a(v.enabled, v.kind, v.type, v.position, v.direction, v.attenuation, v.spot_exponent, v.spot_cutoff, v.ambient, v.diffuse, v.specular); }
    template<class A> static void visit(A &a, gpu::LightingState &v) { a(v.material_update, v.material_emissive, v.material_diffuse, v.material_specular, v.specular_power, v.ambient_color, v.ambient_alpha, v.mode, v.reverse_normals, v.lights); }
    template<class A> static void visit(A &a, gpu::FogState &v) { a(v.enabled, v.end, v.scale, v.color); }
    template<class A> static void visit(A &a, rtc::Rtc &v) { a(v.boot_local_unix_us_); }
    template<class A> static void visit(A &a, modules::UtilityModules &v) { a(v.loaded_); }
    template<class A> static void visit(A &a, modules::ModuleManager &v) { a(v.main_uid_, v.modules_); }
    template<class A> static void visit(A &a, modules::ModuleManager::Module &v) { a(v.path, v.started); }
    template<class A> static void visit(A &a, audio::Audio &v) { a(v.channels_); }
    template<class A> static void visit(A &a, audio::Audio::Channel &v) { a(v.reserved, v.sample_count, v.format, v.left_volume, v.right_volume, v.queued_until_us, v.samples_output); }
    template<class A> static void visit(A &a, sas::VagDecoder &v) { a(v.start_, v.size_, v.block_, v.loop_block_, v.loop_, v.has_loop_start_, v.ended_, v.end_after_block_, v.samples_, v.index_, v.history1_, v.history2_); }
    template<class A> static void visit(A &a, sas::Envelope &v) { a(v.attack, v.decay, v.sustain, v.release, v.sustain_level, v.phase_, v.height_); }
    template<class A> static void visit(A &a, sas::Envelope::Stage &v) { a(v.curve, v.rate); }
    template<class A> static void visit(A &a, sas::SasCore &v) { a(v.core_, v.grain_, v.max_voices_, v.output_mode_, v.voices_, v.noise_state_, v.reverb_type_, v.reverb_delay_, v.reverb_feedback_, v.reverb_left_, v.reverb_right_, v.dry_on_, v.wet_on_); }
    template<class A> static void visit(A &a, sas::SasCore::Voice &v) { a(v.configured, v.on, v.paused, v.noise, v.primed, v.vag_address, v.vag_size, v.loop, v.vag, v.pitch, v.noise_frequency, v.left, v.right, v.effect_left, v.effect_right, v.envelope, v.position, v.current, v.following); }
    template<class A> static void visit(A &a, audio::TrackInfo &v) { a(v.codec, v.channels, v.block_align, v.extradata, v.file_size, v.data_offset, v.data_size, v.end_sample, v.loop_start, v.loop_end, v.skip); }
    template<class A> static void visit(A &a, audio::AtracLibrary::Context &v) { a(v.requested, v.has_data, v.track, v.buffer, v.buffer_size, v.all_on_memory, v.loop_num, v.position, v.pcm, v.pcm_frame, v.next_decoder_frame, v.queue, v.pending, v.next_supply_frame, v.supply_finished, v.decoder_history, v.history_bytes, v.history_complete); }
    template<class A> static void visit(A &a, movie::PsmfPlayer &v) { a(v.players_, v.shown_); }
    template<class A> static void visit(A &a, movie::PsmfPlayer::Player &v) { a(v.status, v.pixel_type, v.loop, v.path, v.file, v.info, v.demuxer, v.next_pack, v.video, v.audio, v.picture, v.picture_pts, v.pictures_out, v.video_ended, v.start_us); }
    template<class A> static void visit(A &a, movie::PsmfInfo &v) { a(v.stream_offset, v.stream_size, v.start_pts, v.end_pts, v.video_streams, v.audio_streams); }
    template<class A> static void visit(A &a, movie::PsmfDemuxer &v) { a(v.video_, v.video_scanned_, v.video_starts_, v.video_stamps_, v.video_units_, v.last_video_pts_, v.last_video_dts_, v.ended_, v.audio_, v.audio_stamps_, v.audio_units_, v.last_audio_pts_, v.audio_frame_size_, v.audio_channels_); }
    template<class A> static void visit(A &a, movie::PsmfDemuxer::Stamp &v) { a(v.offset, v.pts, v.dts); }
    template<class A> static void visit(A &a, movie::AccessUnit &v) { a(v.data, v.pts, v.dts); }
    template<class A> static void visit(A &a, movie::Picture &v) { a(v.width, v.height, v.full_range, v.y, v.cb, v.cr); }
    template<class A> static void visit(A &a, utility::DialogLifecycle &v) { a(v.status_); }

    template<class A> static void visit(A &a, audio::AtracLibrary &v) {
        if constexpr (!A::reading) for (const auto &entry : v.contexts_)
            if (entry && !entry->history_complete) throw FormatError("Audio history is too large to save; try after the track loops");
        a(v.contexts_);
        if constexpr (A::reading) for (auto &entry : v.contexts_) {
            if (!entry || !entry->has_data) continue;
            auto &c = *entry;
            if (c.track.channels < 1 || c.track.channels > 2 || c.track.block_align == 0 || c.track.block_align > 65536)
                throw FormatError("Invalid saved ATRAC track");
            c.decoder = v.factory_();
            if (!c.decoder || !c.decoder->open(c.track.codec,c.track.channels,c.track.block_align,c.track.extradata))
                throw FormatError("Cannot restore the audio decoder");
            std::vector<std::int16_t> scratch(audio::atrac_frame_samples(c.track.codec)*2);
            for (const auto &frame : c.decoder_history) (void)c.decoder->decode(frame,scratch.data());
        }
    }
    template<class A> static void visit(A &a, movie::AvcDecoder &v) {
        if constexpr (!A::reading) if (!v.history_complete_) throw FormatError("Movie history is too large to save; try after the movie");
        bool opened = v.is_open(); a(opened);
        auto history = v.history_; a(history);
        if constexpr (A::reading) {
            v.close();
            if (opened) {
                if (!v.open()) throw FormatError("Cannot restore the movie decoder");
                movie::Picture scratch;
                for (const auto &unit : history) {
                    if (unit.empty()) (void)v.drain(scratch); else (void)v.decode(unit,scratch);
                }
            }
        }
    }
    template<class A> static void visit(A &a, audio::AtracDecoder &v) {
        if constexpr (!A::reading) if (!v.history_complete_) throw FormatError("Movie history is too large to save; try after the movie");
        bool opened = v.is_open(); a(opened);
        auto codec=v.saved_codec_; auto channels=v.saved_channels_; auto align=v.saved_align_;
        auto extra=v.saved_extra_; auto history=v.history_; a(codec,channels,align,extra,history);
        if constexpr (A::reading) if (opened) {
            if (!v.open(codec,channels,align,extra)) throw FormatError("Cannot restore the movie audio decoder");
            std::vector<std::int16_t> scratch(audio::atrac_frame_samples(codec)*2);
            for (const auto &frame : history) (void)v.decode(frame,scratch.data());
        }
    }
    static audio::AtracLibrary empty_atrac(GameSession &g) { return audio::AtracLibrary(g.atrac.factory_); }
    static movie::PsmfPlayer empty_movie(GameSession &g) { return movie::PsmfPlayer(g.kernel,g.psmf_player.reader_); }
    static void camera(game::GameplayCamera &to, const game::GameplayCamera &from) noexcept {
        to.seen_=from.seen_; to.last_frame_=from.last_frame_; to.calls_=from.calls_;
    }
};
template<class A,class T> void fields(A &a,T &v) { SessionCodec::visit(a,v); }
struct NativeState::Data {
    GameSession *destination;
    StartupHle startup_hle;
    umd::UmdDrive umd;
    io::IoManager io;
    ctrl::Controller controller;
    display::Display display;
    game::FrameRateOverride frame_rate;
    game::IntroLogos intro_logos;
    game::OrbitCamera orbit_camera;
    ge::Ge ge;
    rtc::Rtc rtc;
    modules::UtilityModules utility_modules;
    modules::ModuleManager module_manager;
    audio::Audio audio;
    sas::SasCore sas;
    game::GameplayCamera camera;
    audio::AtracLibrary atrac;
    movie::PsmfPlayer psmf_player;
    utility::DialogLifecycle dialog;
    std::vector<std::uint8_t> kernel, ram, vram, scratch, gpu;
    psprecomp::AllegrexContext cpu;
    psprecomp::Runtime::DispatchState dispatch;
    explicit Data(GameSession &g) : destination(&g), startup_hle(g.startup_hle), umd(g.umd), io(g.io), controller(g.controller), display(g.display), frame_rate(g.frame_rate), intro_logos(g.intro_logos), orbit_camera(g.orbit_camera), ge(g.ge), rtc(g.rtc), utility_modules(g.utility_modules), module_manager(g.module_manager), audio(g.audio), sas(g.sas), camera(g.display), atrac(SessionCodec::empty_atrac(g)), psmf_player(SessionCodec::empty_movie(g)), dialog(g.savedata.dialog) {
        SessionCodec::camera(camera,g.gameplay_camera);
    }
    template<class A> void contents(A &a) {
        a(startup_hle, umd, io, controller, display, frame_rate, intro_logos, orbit_camera, ge, rtc, utility_modules, module_manager, audio, sas, camera, atrac, psmf_player, dialog, kernel, ram, vram, scratch, gpu);
        a(cpu.gpr,cpu.hi,cpu.lo,cpu.pc,cpu.fpr,cpu.fcr31,cpu.vfpu,cpu.vfpu_ctrl,cpu.next_pc,cpu.in_delay_slot,cpu.load_linked,dispatch.since_import,dispatch.work);
    }
};
namespace {
std::string host_identity() {
    return std::string(SARUSARU_NATIVE_STATE_ABI) + ":" + std::to_string(sizeof(std::size_t)) + ":" +
        std::to_string(std::endian::native == std::endian::little);
}
void ready(GameSession &g) {
    if (!g.runtime.can_snapshot()) throw FormatError("Save/load requires an outer dispatch boundary");
}
}
NativeState::NativeState(std::unique_ptr<Data> data) : data_(std::move(data)) {}
NativeState::~NativeState() = default;
NativeState::NativeState(NativeState &&) noexcept = default;
NativeState &NativeState::operator=(NativeState &&) noexcept = default;
void NativeState::save(GameSession &g,const std::filesystem::path &path,const std::string &configuration,
                       std::span<const std::uint8_t> renderer, const Thumbnail &thumbnail) {
    ready(g);
    // Serialize the live modules directly: the archive never mutates when writing.
    NativeArchive<false> a;
    std::string identity=host_identity(), config=configuration;
    a.output.section("BigMissionState",1); a(identity,config);
    a(g.startup_hle,g.umd,g.io,g.controller,g.display,g.frame_rate,g.intro_logos,g.orbit_camera,g.ge,g.rtc,
      g.utility_modules,g.module_manager,g.audio,g.sas,g.gameplay_camera,g.atrac,g.psmf_player,g.savedata.dialog);
    auto kernel=KernelSnapshot::capture(g.kernel,g.sync,g.interrupts).encode();
    auto ram=g.runtime.memory().bytes(), vram=g.runtime.memory().vram_bytes();
    std::vector<std::uint8_t> scratch(psprecomp::GuestMemory::kScratchpadSize);
    g.runtime.memory().copy_out(psprecomp::GuestMemory::kScratchpadBase,scratch);
    std::vector<std::uint8_t> gpu(renderer.begin(),renderer.end());
    a(kernel,ram,vram,scratch,gpu);
    auto &cpu=g.runtime.cpu();
    a(cpu.gpr,cpu.hi,cpu.lo,cpu.pc,cpu.fpr,cpu.fcr31,cpu.vfpu,cpu.vfpu_ctrl,cpu.next_pc,cpu.in_delay_slot,cpu.load_linked);
    auto dispatch=g.runtime.dispatch_state(); a(dispatch.since_import,dispatch.work);
    a.output.end_section();
    PpstFile file; file.creator="BigMission native state"; file.title="Native quick state";
    file.payload=a.output.data();
    const auto digest=psprecomp::sha256(file.payload);
    PayloadWriter checked; checked.string(digest); checked.bytes(file.payload); file.payload=checked.data();
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    write_slot(path,file,thumbnail);
}
NativeState NativeState::prepare(GameSession &g,const std::filesystem::path &path,const std::string &configuration) {
    ready(g);
    if (!std::filesystem::is_regular_file(path)) throw FormatError("No saved state yet");
    auto file=read_slot(path);
    PayloadReader checked(file.payload); const auto digest=checked.string();
    auto payload=checked.bytes(checked.remaining());
    if (psprecomp::sha256(payload)!=digest) throw FormatError("Save state checksum mismatch");
    NativeArchive<true> a(payload); (void)a.input->section("BigMissionState",1,1);
    std::string identity,config; a(identity,config);
    if (identity!=host_identity()) throw FormatError("Save state belongs to a different runtime; use the build that saved it");
    if (config!=configuration) throw FormatError("Save state requires the same game text and graphics settings");
    auto data=std::make_unique<Data>(g); data->contents(a); a.input->end_section(); a.input->finish();
    if (data->ram.size()!=g.runtime.memory().size() || data->vram.size()!=g.runtime.memory().vram_size() ||
        data->scratch.size()!=psprecomp::GuestMemory::kScratchpadSize || data->cpu.gpr[0]!=0 || (data->cpu.pc&3)!=0)
        throw FormatError("Invalid saved memory or CPU layout");
    // Kernel validation is repeated immediately before the transactional commit.
    auto kernel=KernelSnapshot::decode(data->kernel); kernel.validate(g.kernel);
    return NativeState(std::move(data));
}
std::span<const std::uint8_t> NativeState::renderer() const {
    if (!data_) throw FormatError("Prepared state has been consumed");
    return data_->gpu;
}
void NativeState::apply(GameSession &g) && {
    ready(g);
    if (!data_ || data_->destination!=&g) throw FormatError("Prepared state belongs to another session");
    auto kernel=KernelSnapshot::decode(data_->kernel);
    std::move(kernel).restore(g.kernel,g.sync,g.interrupts);
    // Every remaining swap is non-allocating; retain original module addresses
    // so HLE lambdas and renderer/audio sinks still refer to the live session.
    using std::swap;
    swap(g.startup_hle,data_->startup_hle);
    swap(g.umd,data_->umd);
    swap(g.io,data_->io);
    swap(g.controller,data_->controller);
    swap(g.display,data_->display);
    swap(g.frame_rate,data_->frame_rate);
    swap(g.intro_logos,data_->intro_logos);
    swap(g.orbit_camera,data_->orbit_camera);
    swap(g.ge,data_->ge);
    swap(g.rtc,data_->rtc);
    swap(g.utility_modules,data_->utility_modules);
    swap(g.module_manager,data_->module_manager);
    swap(g.audio,data_->audio);
    swap(g.sas,data_->sas);
    SessionCodec::camera(g.gameplay_camera,data_->camera);
    swap(g.atrac,data_->atrac); swap(g.psmf_player,data_->psmf_player); swap(g.savedata.dialog,data_->dialog);
    g.runtime.memory().copy_in(psprecomp::GuestMemory::kPhysicalBase,data_->ram);
    g.runtime.memory().copy_in(psprecomp::GuestMemory::kVramPhysicalBase,data_->vram);
    g.runtime.memory().copy_in(psprecomp::GuestMemory::kScratchpadBase,data_->scratch);
    g.runtime.cpu()=data_->cpu;
    g.runtime.restore_dispatch_state(data_->dispatch);
    g.restore_observers();
    g.controller.set_pad({}); g.orbit_camera.set_input({});
    data_.reset();
}
}
