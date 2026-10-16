#include "save_state/slots.hpp"
#include "platform/paths.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f,const char *message) {
    bool failed=false; try { f(); } catch (const std::exception &) { failed=true; } require(failed,message);
}
}
int main() {
    using namespace sarusaru::save_state;
    const auto directory=std::filesystem::temp_directory_path()/("bigmission-slots-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(directory/"states");
        const auto settings=directory/"settings.ini";
        std::set<std::filesystem::path> paths;
        for (std::uint32_t slot=1;slot<=kSlotCount;++slot) {
            const auto path=slot_path(settings,slot);
            require(paths.insert(path).second,"slots share a file");
            require(!slot_info(path).occupied,"missing slot is occupied");
            PpstFile state; state.creator="slot-test"; state.payload={static_cast<std::uint8_t>(slot),2,3,4};
            Thumbnail thumbnail{kThumbnailWidth,kThumbnailHeight,std::vector<std::uint8_t>(kThumbnailWidth*kThumbnailHeight*4,static_cast<std::uint8_t>(slot))};
            write_slot(path,state,thumbnail);
            const auto info=slot_info(path);
            require(info.occupied && info.saved_at>0 && info.thumbnail==thumbnail,"slot preview did not round trip");
            require(read_slot(path).payload==state.payload,"preview wrapper damaged state payload");
            auto bad=thumbnail; bad.width=1;
            rejects([&] { write_slot(path,state,bad); },"invalid thumbnail accepted");
            require(slot_info(path).thumbnail==thumbnail && read_slot(path).payload==state.payload,"failed save changed slot");
        }
        for (std::uint32_t slot=1;slot<=kSlotCount;++slot)
            require(read_slot(slot_path(settings,slot)).payload[0]==slot,"saving another slot changed this slot");
        rejects([&] { (void)slot_path(settings,0); },"slot zero accepted");
        rejects([&] { (void)slot_path(settings,6); },"sixth slot accepted");
        require(slot_path(settings,1).filename()=="UCJS10060.quick.bmstate","original quick slot was moved");
        PpstFile legacy; legacy.creator="legacy"; legacy.payload={9,8,7};
        write_ppst(slot_path(settings,1),legacy);
        require(slot_info(slot_path(settings,1)).occupied && slot_info(slot_path(settings,1)).thumbnail.rgba.empty(),"legacy slot hidden");
        require(read_slot(slot_path(settings,1)).payload==legacy.payload,"legacy container rejected");
        // A corrupt preview must neither allocate arbitrary sizes nor hide the file.
        const auto bad_path=slot_path(settings,5);
        { std::fstream file(bad_path,std::ios::in|std::ios::out|std::ios::binary); file.seekp(20); const std::array<char,4> huge{char(255),char(255),char(255),char(255)}; file.write(huge.data(),4); }
        require(slot_info(bad_path).occupied && slot_info(bad_path).thumbnail.rgba.empty(),"corrupt preview not handled");
        rejects([&] { (void)read_slot(bad_path); },"corrupt slot dimensions accepted");
        const auto url=sarusaru::platform::directory_url(directory/"regular saves #1%"/"PSP"/"SAVEDATA");
        require(url.starts_with("file:") && url.find("regular%20saves%20%231%25/PSP/SAVEDATA")!=std::string::npos,"saves directory URL was not escaped");
        std::filesystem::remove_all(directory);
        std::cout<<"PASS: five independent state slots, atomic previews, legacy files and folder URLs\n";
    } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; std::filesystem::remove_all(directory); return 1; }
}
