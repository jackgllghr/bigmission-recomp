#include "session.hpp"
#include "save_state/native_state.hpp"
#include "save_state/ppst.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <tuple>

void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    namespace ss=sarusaru::save_state;
    const auto dir=std::filesystem::temp_directory_path()/"bigmission-native-state-test";
    std::filesystem::create_directories(dir);
    const auto path=dir/"quick.bmstate";
    try {
        sarusaru::SessionOptions options; options.game_module=argv[2]; options.memory_stick=dir/"memstick";
        const std::vector<std::uint8_t> gpu{1,2,3,4};
        const ss::Thumbnail preview{ss::kThumbnailWidth,ss::kThumbnailHeight,std::vector<std::uint8_t>(ss::kThumbnailWidth*ss::kThumbnailHeight*4,42)};
        std::uint32_t entry{};
        std::vector<std::uint8_t> expected_video, expected_audio, expected_atrac, expected_sas;
        std::int32_t atrac_id=-1;
        const bool media=sarusaru::movie::AvcDecoder::available();
        constexpr std::uint32_t movie_handle=0x1234, video_buffer=0x09B00000, audio_buffer=0x09C00000, track_buffer=0x09500000;
        const auto next_sas=[](sarusaru::GameSession &game) {
            constexpr std::uint32_t output=0x08106000;
            require(game.sas.render(game.runtime.memory(),output,false,0,0)==0,"render restored SAS reverb");
            std::vector<std::uint8_t> pcm(game.sas.grain()*4);
            game.runtime.memory().copy_out(output,pcm);
            return pcm;
        };
        const auto next_media=[&](sarusaru::GameSession &game) {
            auto clock=game.kernel.snapshot(); clock.now_us+=100'000; game.kernel.restore(std::move(clock));
            std::uint32_t pts{};
            require(game.psmf_player.video(game.runtime.memory(),movie_handle,512,video_buffer,pts)==0,"decode restored movie picture");
            require(game.psmf_player.audio(game.runtime.memory(),movie_handle,audio_buffer)==0,"decode restored movie audio");
            sarusaru::audio::AtracLibrary::Decoded decoded;
            require(game.atrac.decode(static_cast<std::uint32_t>(atrac_id),game.runtime.memory(),audio_buffer+0x4000,decoded)==0,"decode restored ATRAC track");
            std::vector<std::uint8_t> video(512*272*4), audio(8192), atrac(8192);
            game.runtime.memory().copy_out(video_buffer,video); game.runtime.memory().copy_out(audio_buffer,audio);
            game.runtime.memory().copy_out(audio_buffer+0x4000,atrac);
            return std::tuple{std::move(video),std::move(audio),std::move(atrac)};
        };
        {
            auto source=std::make_unique<sarusaru::GameSession>(argv[1],options);
            entry=source->entry(); source->runtime.cpu().pc=entry;
            source->runtime.cpu().set_gpr(17,0xAABBCCDD);
            source->runtime.memory().store32(0x08105000,0x12345678);
            source->runtime.memory().store32(0x04000000,0x87654321);
            source->runtime.memory().store32(0x00010000,0x13572468);
            (void)source->controller.set_sampling_cycle(6000);
            require(source->audio.reserve(0,256,sarusaru::audio::kFormatStereo)==0,"reserve audio");
            const auto sem=source->sync.create_semaphore("state semaphore",0,2,5);
            require(sem>0,"create semaphore");
            require(source->io.open("disc0:/UMD_DATA.BIN",1)==3,"open disc file");
            require(source->io.seek(3,7,0)==7,"seek disc file");
            if (media) {
                require(source->psmf_player.create(movie_handle)==0,"create movie");
                require(source->psmf_player.set_psmf(movie_handle,"disc0:/PSP_GAME/USRDIR/movie/d01.pmf")==0,"open movie");
                require(source->psmf_player.start(source->runtime,movie_handle,0)==0,"start movie");
                auto bytes=source->io.read_disc_file("disc0:/PSP_GAME/SND0.AT3"); require(bytes.has_value(),"read audio fixture");
                source->runtime.memory().copy_in(track_buffer,*bytes);
                atrac_id=source->atrac.get_id(sarusaru::audio::kCodecAtrac3Plus);
                auto result=source->atrac.set_data(static_cast<std::uint32_t>(atrac_id),source->runtime.memory(),track_buffer,static_cast<std::uint32_t>(bytes->size()));
                if (static_cast<std::uint32_t>(result)==sarusaru::audio::atrac_error::kBadCodecType) {
                    (void)source->atrac.release(static_cast<std::uint32_t>(atrac_id));
                    atrac_id=source->atrac.get_id(sarusaru::audio::kCodecAtrac3);
                    result=source->atrac.set_data(static_cast<std::uint32_t>(atrac_id),source->runtime.memory(),track_buffer,static_cast<std::uint32_t>(bytes->size()));
                }
                require(result==0,"open ATRAC fixture");
                (void)next_media(*source); (void)next_media(*source);
            }
            // Main added persistent DSP and stencil state; both must survive a fresh session.
            require(source->sas.init(0x08108000,256,32,0,44100)==0 &&
                    source->sas.set_noise(0,17)==0 &&
                    source->sas.set_volume(0,0,0,0x1000,0x800)==0 &&
                    source->sas.set_simple_adsr(0,0x0007,0x1FC0)==0 && source->sas.key_on(0)==0 &&
                    source->sas.set_reverb_type(6)==0 && source->sas.set_reverb_parameters(1,75)==0 &&
                    source->sas.set_reverb_volume(0x1000,0x1000)==0 && source->sas.set_reverb_voice_on(0,1)==0,
                    "configure SAS reverb snapshot");
            for (int i=0;i<16;++i) (void)next_sas(*source);
            for (const auto word : {0x24000001u,0xdc7f0a02u,0xdd020103u,0xe8ff00ffu,0xe900000fu})
                source->ge.state().process(source->runtime.memory(),0,word);
            ss::NativeState::save(*source,path,"test",gpu,preview);
            require(ss::slot_info(path).thumbnail==preview,"native state thumbnail was not committed");
            expected_sas=next_sas(*source);
            require(std::any_of(expected_sas.begin(),expected_sas.end(),[](auto v) { return v!=0; }),"SAS reverb fixture is silent");
            if (media) std::tie(expected_video,expected_audio,expected_atrac)=next_media(*source);
        }
        // The source is gone: host bindings must belong to this fresh session.
        auto destination=std::make_unique<sarusaru::GameSession>(argv[1],options);
        destination->runtime.cpu().pc=destination->entry();
        auto state=ss::NativeState::prepare(*destination,path,"test");
        require(std::vector<std::uint8_t>(state.renderer().begin(),state.renderer().end())==gpu,"renderer payload");
        std::move(state).apply(*destination);
        require(destination->runtime.cpu().pc==entry && destination->runtime.cpu().gpr[17]==0xAABBCCDD,"CPU did not restore");
        require(destination->runtime.memory().load32(0x08105000)==0x12345678,"RAM did not restore");
        require(destination->runtime.memory().load32(0x04000000)==0x87654321,"VRAM did not restore");
        require(destination->runtime.memory().load32(0x00010000)==0x13572468,"scratchpad did not restore");
        require(destination->controller.sampling_cycle()==6000,"controller did not restore");
        require(destination->audio.channel(0).reserved,"audio did not restore");
        require(destination->io.seek(3,0,1)==7,"file position did not restore");
        require(next_sas(*destination)==expected_sas,"restored SAS reverb continuation differs");
        bool saw_stencil=false;
        destination->ge.state().set_draw_sink([&](const sarusaru::gpu::DrawCall &draw) {
            const auto &v=draw.stencil;
            saw_stencil=v.test_enabled && v.function==2 && v.reference==0x0a && v.mask==0x7f &&
                v.fail==3 && v.depth_fail==1 && v.pass==2 && v.color_write_mask==0xff00ff && v.alpha_write_mask==0x0f;
        });
        for (const auto word : {0x10080000u,0x12000180u,0x01010700u,0x04000001u})
            destination->ge.state().process(destination->runtime.memory(),0,word);
        require(saw_stencil,"GE stencil state did not restore");
        if (media) {
            auto [video,audio,atrac]=next_media(*destination);
            require(video==expected_video,"restored H.264 continuation differs");
            require(audio==expected_audio,"restored movie audio continuation differs");
            require(atrac==expected_atrac,"restored ATRAC continuation differs");
        }
        auto baseline=destination->runtime.memory().load32(0x08105000);
        bool rejected=false;
        try { (void)ss::NativeState::prepare(*destination,path,"other settings"); } catch (const ss::FormatError &) { rejected=true; }
        require(rejected,"configuration mismatch accepted");
        auto file=ss::read_slot(path); file.payload.back()^=1; ss::write_slot(path,file,preview);
        rejected=false;
        try { (void)ss::NativeState::prepare(*destination,path,"test"); } catch (const ss::FormatError &) { rejected=true; }
        require(rejected && destination->runtime.memory().load32(0x08105000)==baseline,"corrupt load changed live memory");
        // Failed save must preserve the existing slot.
        std::vector<std::uint8_t> oversized(ss::kMaxPpstSize,0);
        rejected=false;
        try { ss::NativeState::save(*destination,path,"test",oversized); } catch (const ss::FormatError &) { rejected=true; }
        require(rejected && ss::read_slot(path).payload==file.payload,"failed save replaced the slot");
        std::filesystem::remove_all(dir);
        std::cout << "PASS: native state across fresh sessions, corruption and configuration rejection\n";
    } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; std::filesystem::remove_all(dir); return 1; }
}
