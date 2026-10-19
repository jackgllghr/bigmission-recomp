#include "gpu/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// ARMv8 SHA-256 instructions: always there when the build targets them
// (Apple silicon), else checked for at run time (Android and Linux builds
// target plain ARMv8). x86 uses the portable rounds.
#if defined(__aarch64__) && defined(__ARM_FEATURE_SHA2)
#include <arm_neon.h>
#define SARUSARU_SHA256_ARM 1
#define SARUSARU_SHA256_ARM_TARGET
#elif defined(__aarch64__) && defined(__clang__) && defined(__linux__)
#include <arm_neon.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>
#define SARUSARU_SHA256_ARM 1
#define SARUSARU_SHA256_ARM_TARGET __attribute__((target("sha2")))
#define SARUSARU_SHA256_ARM_RUNTIME 1
#endif



namespace sarusaru::gpu {
namespace {
constexpr std::array<std::uint32_t, 64> K{
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32u - n)); }

void transform_portable(std::array<std::uint32_t, 8> &h, const std::uint8_t *data, std::size_t blocks) {
    for (; blocks != 0u; --blocks, data += 64u) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(data[i * 4]) << 24u) | (static_cast<std::uint32_t>(data[i * 4 + 1]) << 16u) |
                   (static_cast<std::uint32_t>(data[i * 4 + 2]) << 8u) | data[i * 4 + 3];
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3u);
            const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10u);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (std::size_t i = 0; i < 64; ++i) {
            const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const auto ch = (e & f) ^ ((~e) & g);
            const auto t1 = hh + s1 + ch + K[i] + w[i];
            const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + s0 + maj;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
}

#if SARUSARU_SHA256_ARM
// ARMv8 SHA-256 instructions: four rounds each, with the message schedule.
SARUSARU_SHA256_ARM_TARGET void transform_arm(std::array<std::uint32_t, 8> &h, const std::uint8_t *data,
                                               std::size_t blocks) {
    uint32x4_t abcd = vld1q_u32(h.data()), efgh = vld1q_u32(h.data() + 4);
    for (; blocks != 0u; --blocks, data += 64u) {
        const uint32x4_t abcd_saved = abcd, efgh_saved = efgh;
        uint32x4_t message[4];
        for (int i = 0; i < 4; ++i) message[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + i * 16)));
        for (int i = 0; i < 16; ++i) {
            const uint32x4_t wk = vaddq_u32(message[i & 3], vld1q_u32(K.data() + i * 4));
            const uint32x4_t previous = abcd;
            abcd = vsha256hq_u32(abcd, efgh, wk);
            efgh = vsha256h2q_u32(efgh, previous, wk);
            if (i < 12)
                message[i & 3] = vsha256su1q_u32(vsha256su0q_u32(message[i & 3], message[(i + 1) & 3]),
                                                 message[(i + 2) & 3], message[(i + 3) & 3]);
        }
        abcd = vaddq_u32(abcd, abcd_saved);
        efgh = vaddq_u32(efgh, efgh_saved);
    }
    vst1q_u32(h.data(), abcd);
    vst1q_u32(h.data() + 4, efgh);
}
#endif

void transform(std::array<std::uint32_t, 8> &h, const std::uint8_t *data, std::size_t blocks) {
#if SARUSARU_SHA256_ARM_RUNTIME
    static const bool hardware = (getauxval(AT_HWCAP) & HWCAP_SHA2) != 0u;
    if (hardware) transform_arm(h, data, blocks);
    else transform_portable(h, data, blocks);
#elif SARUSARU_SHA256_ARM
    transform_arm(h, data, blocks);
#else
    transform_portable(h, data, blocks);
#endif
}

std::string to_hex(std::span<const std::uint8_t> digest) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(digest.size() * 2u, '0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[i * 2] = kDigits[digest[i] >> 4u];
        out[i * 2 + 1] = kDigits[digest[i] & 15u];
    }
    return out;
}
} // namespace

void Sha256::update(std::span<const std::uint8_t> bytes) {
    length_ += bytes.size();
    const std::uint8_t *data = bytes.data();
    std::size_t size = bytes.size();
    if (buffered_ != 0u) {
        const std::size_t take = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, data, take);
        buffered_ += take;
        data += take;
        size -= take;
        if (buffered_ < buffer_.size()) return;
        transform(state_, buffer_.data(), 1u);
        buffered_ = 0u;
    }
    if (size >= 64u) {
        transform(state_, data, size / 64u);
        data += size / 64u * 64u;
        size %= 64u;
    }
    if (size != 0u) {
        std::memcpy(buffer_.data(), data, size);
        buffered_ = size;
    }
}

std::array<std::uint8_t, 32> Sha256::digest() {
    const std::uint64_t bits = length_ * 8u;
    std::array<std::uint8_t, 72> tail{};
    tail[0] = 0x80u;
    const std::size_t padding = (buffered_ < 56u ? 56u : 120u) - buffered_;
    for (int i = 0; i < 8; ++i) tail[padding + i] = static_cast<std::uint8_t>(bits >> ((7 - i) * 8));
    update(std::span(tail).first(padding + 8u));
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t b = 0; b < 4; ++b) out[i * 4 + b] = static_cast<std::uint8_t>(state_[i] >> (24u - b * 8u));
    return out;
}

std::string Sha256::hex() {
    const auto bytes = digest();
    return to_hex(bytes);
}

std::string sha256_portable(std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8u;
    data.push_back(0x80u);
    while ((data.size() % 64u) != 56u) data.push_back(0u);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<std::uint8_t>((bits >> (i * 8)) & 0xFFu));
    std::array<std::uint32_t, 8> h{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    transform_portable(h, data.data(), data.size() / 64u);
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t b = 0; b < 4; ++b) out[i * 4 + b] = static_cast<std::uint8_t>(h[i] >> (24u - b * 8u));
    return to_hex(out);
}
} // namespace sarusaru::gpu
