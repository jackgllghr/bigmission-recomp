// Adapted from Yakumo (https://github.com/TeamGDB/Yakumo, revision 39ac05f),
// MIT License; see profiles/sarusaru/THIRD_PARTY.md.
#include "save_data/aes128.hpp"

namespace sarusaru::savedata {

Aes128::Aes128(const Block &key) {
    AES_init_ctx(&context_, key.data());
}

Block Aes128::encrypt(const Block &input) const {
    Block block = input;
    AES_ECB_encrypt(&context_, block.data());
    return block;
}

Block Aes128::decrypt(const Block &input) const {
    Block block = input;
    AES_ECB_decrypt(&context_, block.data());
    return block;
}

} // namespace sarusaru::savedata
