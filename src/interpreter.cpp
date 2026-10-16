#include "psprecomp/interpreter.hpp"
#include "psprecomp/decoder.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace psprecomp {
namespace {
using K = OpcodeKind;

bool likely(K k) {
    return k == K::Beql || k == K::Bnel || k == K::Blezl || k == K::Bgtzl ||
           k == K::Bltzl || k == K::Bgezl || k == K::Bltzall || k == K::Bgezall ||
           k == K::Bc1fl || k == K::Bc1tl || k == K::Bvfl || k == K::Bvtl;
}

bool condition(const DecodedInstruction &d, const AllegrexContext &c) {
    const auto a = c.gpr[d.rs], b = c.gpr[d.rt];
    switch (d.kind) {
    case K::Beq: case K::Beql: return a == b;
    case K::Bne: case K::Bnel: return a != b;
    case K::Blez: case K::Blezl: return static_cast<std::int32_t>(a) <= 0;
    case K::Bgtz: case K::Bgtzl: return static_cast<std::int32_t>(a) > 0;
    case K::Bltz: case K::Bltzl: case K::Bltzal: case K::Bltzall: return static_cast<std::int32_t>(a) < 0;
    case K::Bgez: case K::Bgezl: case K::Bgezal: case K::Bgezall: return static_cast<std::int32_t>(a) >= 0;
    case K::Bc1f: case K::Bc1fl: return !c.fpu_condition();
    case K::Bc1t: case K::Bc1tl: return c.fpu_condition();
    case K::Bvf: case K::Bvfl: return ((c.vfpu_ctrl[3] >> ((d.word >> 18u) & 7u)) & 1u) == 0;
    case K::Bvt: case K::Bvtl: return ((c.vfpu_ctrl[3] >> ((d.word >> 18u) & 7u)) & 1u) != 0;
    default: return false;
    }
}

bool vfpu(const DecodedInstruction &d, AllegrexContext &c, GuestMemory &memory) {
    const auto w = d.word;
    const auto vd = w & 127u, vs = (w >> 8u) & 127u, vt = (w >> 16u) & 127u;
    const auto n = 1u + ((w >> 7u) & 1u) + (((w >> 15u) & 1u) << 1u);
    const auto op = (w >> 16u) & 31u;
    switch (d.kind) {
    case K::Vflush: if ((w & 0xFFFF0000u) != 0xFFFF0000u) c.eat_vfpu_prefixes(); break;
    case K::Vpfx: {
        const auto control = (w >> 24u) & 3u;
        c.vfpu_ctrl[control] = w & (control == 2u ? 0xFFFu : 0xFFFFFu);
        break;
    }
    case K::Mtv: c.set_vfpu_scalar_bits(w & 255u, c.gpr[d.rt]); break;
    case K::Mfv: c.set_gpr(d.rt, c.vfpu_scalar_bits(w & 255u)); break;
    case K::Vf2h: c.execute_vfpu_vf2h(vd, vs, n); break;
    case K::Vh2f: c.execute_vfpu_vh2f(vd, vs, n); break;
    case K::Vi2x: c.execute_vfpu_vi2x(vd, vs, n, op & 3u); break;
    case K::Vx2i: c.execute_vfpu_vx2i(vd, vs, n, op & 3u); break;
    case K::Vrnd: c.execute_vfpu_vrnd(op & 3u, vd, vs, n); break;
    case K::Vcrs: c.execute_vfpu_vcrs(vd, vs, vt); break;
    case K::VmidT: c.execute_vfpu_matrix_init(vd, n, 3u); break;
    case K::VfpuMatrixInit: c.execute_vfpu_matrix_init(vd, n, op & 15u); break;
    case K::Vmmov: c.execute_vfpu_vmmov(vd, vs, n); break;
    case K::Vmscl: c.execute_vfpu_vmscl(vd, vs, vt, n); break;
    case K::Vrot: c.execute_vfpu_vrot(vd, vs, n, op); break;
    case K::Vocp: c.execute_vfpu_vocp(vd, vs, n); break;
    case K::VfpuHorizontal: c.execute_vfpu_horizontal(vd, vs, n, op == 7u); break;
    case K::Vdot: c.execute_vfpu_vdot(vd, vs, vt, n); break;
    case K::Vhdp: c.execute_vfpu_vhdp(vd, vs, vt, n); break;
    case K::VcrossQuat: c.execute_vfpu_cross_quat(vd, vs, vt, n); break;
    case K::Vminmax: c.execute_vfpu_vminmax(vd, vs, vt, n, ((w >> 23u) & 7u) == 3u); break;
    case K::VfpuCompare3: c.execute_vfpu_compare3(vd, vs, vt, n, (w >> 23u) & 7u); break;
    case K::Vcmp: c.execute_vfpu_vcmp(vs, vt, n, w & 15u); break;
    case K::Vcmov: c.execute_vfpu_vcmov(vd, vs, n, op & 7u, ((w >> 19u) & 1u) != 0); break;
    case K::Vscl: c.execute_vfpu_vscl(vd, vs, vt, n); break;
    case K::Vidt: {
        float values[4]{};
        values[vd & (n >= 3 ? 3u : 1u)] = 1.0f;
        c.write_vfpu_vector_with_destination_prefix(values, vd, n);
        break;
    }
    case K::Viim: case K::Vfim: {
        const float value[1]{d.kind == K::Viim ? static_cast<float>(static_cast<std::int16_t>(w))
                                              : std::bit_cast<float>(AllegrexContext::vfpu_expand_half_bits(static_cast<std::uint16_t>(w)))};
        c.write_vfpu_vector_with_destination_prefix(value, vt, 1u);
        break;
    }
    case K::VfpuVectorInit: {
        const float v = op == 7u ? 1.0f : 0.0f;
        const float values[4]{v, v, v, v};
        c.write_vfpu_vector_with_destination_prefix(values, vd, n);
        break;
    }
    case K::Vcst: {
        constexpr std::uint32_t bits[32]{
            0x00000000u, 0x7F7FFFFFu, 0x3FB504F3u, 0x3F3504F3u,
            0x3F906EBAu, 0x3F22F983u, 0x3EA2F983u, 0x3F490FDBu,
            0x3FC90FDBu, 0x40490FDBu, 0x402DF854u, 0x3FB8AA3Bu,
            0x3EDE5BD9u, 0x3F317218u, 0x40135D8Eu, 0x40C90FDBu,
            0x3F060A92u, 0x3E9A209Bu, 0x40549A78u, 0x3F5DB3D7u};
        const auto v = std::bit_cast<float>(bits[op]);
        const float values[4]{v, v, v, v};
        c.write_vfpu_vector_with_destination_prefix(values, vd, n);
        break;
    }
    case K::VfpuVec3: case K::VfpuUnary: case K::Vi2f: case K::Vf2i: {
        float a[4]{}, b[4]{}, result[4]{};
        c.read_vfpu_vector_with_source_prefix(a, vs, n, 0u);
        if (d.kind == K::VfpuVec3) c.read_vfpu_vector_with_source_prefix(b, vt, n, 1u);
        for (std::uint32_t i = 0; i < n; ++i) {
            if (d.kind == K::VfpuVec3) {
                const auto arithmetic = (w >> 26u) == 0x19u ? 2u : ((w >> 23u) & 7u);
                result[i] = arithmetic == 0u ? a[i] + b[i] : arithmetic == 1u ? a[i] - b[i]
                            : arithmetic == 2u ? a[i] * b[i] : a[i] / b[i];
            } else if (d.kind == K::Vi2f) {
                result[i] = static_cast<float>(static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a[i]))) *
                            std::ldexp(1.0f, -static_cast<int>(op));
            } else if (d.kind == K::Vf2i) {
                const auto mode = (w >> 21u) & 31u;
                const double scaled = static_cast<double>(a[i] * std::ldexp(1.0f, static_cast<int>(op)));
                std::int32_t value{};
                if (std::isnan(scaled) || scaled >= std::numeric_limits<std::int32_t>::max()) value = std::numeric_limits<std::int32_t>::max();
                else if (scaled <= std::numeric_limits<std::int32_t>::min()) value = std::numeric_limits<std::int32_t>::min();
                else {
                    const double rounded = mode == 16u ? AllegrexContext::round_ties_to_even(scaled)
                        : mode == 17u ? std::trunc(scaled) : mode == 18u ? std::ceil(scaled) : std::floor(scaled);
                    value = static_cast<std::int32_t>(rounded);
                }
                if (((c.vfpu_ctrl[2] >> (8u + i)) & 1u) == 0)
                    c.vfpu[AllegrexContext::vfpu_vector_lane_index(vd, n, i)] = std::bit_cast<float>(static_cast<std::uint32_t>(value));
            } else {
                switch (op) {
                case 0: result[i] = a[i]; break;
                case 1: result[i] = std::fabs(a[i]); break;
                case 2: result[i] = -a[i]; break;
                case 4: result[i] = a[i] <= 0.0f ? 0.0f : a[i] > 1.0f ? 1.0f : a[i]; break;
                case 5: result[i] = a[i] < -1.0f ? -1.0f : a[i] > 1.0f ? 1.0f : a[i]; break;
                case 16: result[i] = 1.0f / a[i]; break;
                case 17: result[i] = 1.0f / std::sqrt(a[i]); break;
                case 18: result[i] = std::sin(a[i] * 1.57079632679489661923f); break;
                case 19: result[i] = std::cos(a[i] * 1.57079632679489661923f); break;
                case 20: result[i] = std::exp2(a[i]); break;
                case 21: result[i] = std::log2(a[i]); break;
                case 22: result[i] = std::fabs(std::sqrt(a[i])); break;
                case 23: result[i] = std::asin(a[i]) * 0.63661977236758134308f; break;
                case 24: result[i] = -1.0f / a[i]; break;
                case 26: result[i] = -std::sin(a[i] * 1.57079632679489661923f); break;
                default: result[i] = 1.0f / std::exp2(a[i]); break;
                }
            }
        }
        if (d.kind == K::Vf2i) c.eat_vfpu_prefixes();
        else c.write_vfpu_vector_with_destination_prefix(result, vd, n);
        break;
    }
    case K::Vmmul: {
        float a[16]{}, b[16]{}, result[16]{};
        c.read_vfpu_matrix(a, vs, n);
        c.read_vfpu_matrix(b, vt, n);
        for (std::uint32_t row = 0; row < n; ++row)
            for (std::uint32_t col = 0; col < n; ++col)
                for (std::uint32_t lane = 0; lane < n; ++lane)
                    result[row * 4u + col] += a[col * 4u + lane] * b[row * 4u + lane];
        c.write_vfpu_matrix(result, vd, n);
        c.eat_vfpu_prefixes();
        break;
    }
    case K::Vtfm: {
        const auto side = 1u + ((w >> 23u) & 3u);
        float matrix[16]{}, raw[4]{}, input[4]{}, result[4]{};
        c.read_vfpu_matrix(matrix, vs, side);
        c.read_vfpu_vector(raw, vt, side);
        for (std::uint32_t i = 0; i < 4; ++i) input[i] = i < n ? raw[i] : 0.0f;
        if (side - 1u >= n) input[side - 1u] = 1.0f;
        for (std::uint32_t row = 0; row + 1u < side; ++row)
            for (std::uint32_t col = 0; col < side; ++col) result[row] += matrix[row * 4u + col] * input[col];
        float last[4]{};
        std::copy_n(matrix + (side - 1u) * 4u, 4, last);
        c.apply_vfpu_source_prefix(last, 4u, 0u);
        c.apply_vfpu_source_prefix(input, 4u, 1u);
        for (std::uint32_t col = 0; col < 4; ++col) result[side - 1u] += last[col] * input[col];
        const auto prefix = c.vfpu_ctrl[2];
        c.vfpu_ctrl[2] = ((prefix & (1u << 8u)) << (side - 1u)) | ((prefix & 3u) << ((side - 1u) * 2u));
        c.write_vfpu_vector_with_destination_prefix(result, vd, side);
        break;
    }
    case K::Lvs: case K::Svs: case K::Lvq: case K::Svq: {
        const auto address = c.gpr[d.rs] + static_cast<std::uint32_t>(static_cast<std::int16_t>(w & 0xFFFCu));
        if (d.kind == K::Lvs || d.kind == K::Svs) {
            const auto reg = ((w >> 16u) & 31u) | ((w & 3u) << 5u);
            if (d.kind == K::Lvs) c.set_vfpu_scalar_bits(reg, memory.load32(address));
            else memory.store32(address, c.vfpu_scalar_bits(reg));
        } else {
            const auto reg = ((w >> 16u) & 31u) | ((w & 1u) << 5u);
            float values[4]{};
            if (d.kind == K::Lvq) {
                for (std::uint32_t i = 0; i < 4; ++i) values[i] = std::bit_cast<float>(memory.load32(address + 4u * i));
                c.write_vfpu_vector(values, reg, 4u);
            } else {
                c.read_vfpu_vector(values, reg, 4u);
                for (std::uint32_t i = 0; i < 4; ++i) memory.store32(address + 4u * i, std::bit_cast<std::uint32_t>(values[i]));
            }
        }
        break;
    }
    default: return false;
    }
    return true;
}

void regular(Runtime &rt, AllegrexContext &c, const DecodedInstruction &d) {
    auto &m = rt.memory();
    const auto a = c.gpr[d.rs], b = c.gpr[d.rt];
    const auto sa = static_cast<std::int32_t>(a), sb = static_cast<std::int32_t>(b);
    const auto imm = static_cast<std::int32_t>(d.immediate);
    const auto uimm = static_cast<std::uint16_t>(d.immediate);
    const auto address = a + static_cast<std::uint32_t>(imm);
    switch (d.kind) {
    case K::Nop: case K::Cache: break;
    case K::Sync: m.memory_barrier(); break;
    case K::Addiu: c.set_gpr(d.rt, address); break;
    case K::Slti: c.set_gpr(d.rt, sa < imm); break;
    case K::Sltiu: c.set_gpr(d.rt, a < static_cast<std::uint32_t>(imm)); break;
    case K::Andi: c.set_gpr(d.rt, a & uimm); break;
    case K::Ori: c.set_gpr(d.rt, a | uimm); break;
    case K::Xori: c.set_gpr(d.rt, a ^ uimm); break;
    case K::Lui: c.set_gpr(d.rt, static_cast<std::uint32_t>(uimm) << 16u); break;
    case K::Add: if (!c.execute_signed_add(d.rd, d.rs, d.rt)) rt.arithmetic_overflow(c.pc, d.word); break;
    case K::Addu: c.set_gpr(d.rd, a + b); break;
    case K::Sub: if (!c.execute_signed_sub(d.rd, d.rs, d.rt)) rt.arithmetic_overflow(c.pc, d.word); break;
    case K::Subu: c.set_gpr(d.rd, a - b); break;
    case K::And: c.set_gpr(d.rd, a & b); break;
    case K::Or: c.set_gpr(d.rd, a | b); break;
    case K::Xor: c.set_gpr(d.rd, a ^ b); break;
    case K::Nor: c.set_gpr(d.rd, ~(a | b)); break;
    case K::Slt: c.set_gpr(d.rd, sa < sb); break;
    case K::Sltu: c.set_gpr(d.rd, a < b); break;
    case K::Max: c.set_gpr(d.rd, sa > sb ? a : b); break;
    case K::Min: c.set_gpr(d.rd, sa < sb ? a : b); break;
    case K::Movz: if (b == 0) c.set_gpr(d.rd, a); break;
    case K::Movn: if (b != 0) c.set_gpr(d.rd, a); break;
    case K::Sll: c.set_gpr(d.rd, b << d.sa); break;
    case K::Srl: c.set_gpr(d.rd, b >> d.sa); break;
    case K::Sra: c.set_gpr(d.rd, static_cast<std::uint32_t>(sb >> d.sa)); break;
    case K::Rotr: c.set_gpr(d.rd, std::rotr(b, static_cast<int>(d.sa))); break;
    case K::Sllv: c.set_gpr(d.rd, b << (a & 31u)); break;
    case K::Srlv: c.set_gpr(d.rd, b >> (a & 31u)); break;
    case K::Srav: c.set_gpr(d.rd, static_cast<std::uint32_t>(sb >> (a & 31u))); break;
    case K::Rotrv: c.set_gpr(d.rd, std::rotr(b, static_cast<int>(a & 31u))); break;
    case K::Clz: c.set_gpr(d.rd, static_cast<std::uint32_t>(std::countl_zero(a))); break;
    case K::Clo: c.set_gpr(d.rd, static_cast<std::uint32_t>(std::countl_one(a))); break;
    case K::Ext: {
        const auto n = d.rd + 1u;
        c.set_gpr(d.rt, (a >> d.sa) & (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u));
        break;
    }
    case K::Ins: {
        const auto n = d.rd >= d.sa ? d.rd - d.sa + 1u : 0u;
        const auto mask = n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u;
        c.set_gpr(d.rt, (b & ~(mask << d.sa)) | ((a & mask) << d.sa));
        break;
    }
    case K::Seb: c.set_gpr(d.rd, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(b)))); break;
    case K::Seh: c.set_gpr(d.rd, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(b)))); break;
    case K::Bitrev: {
        auto value = b;
        value = ((value >> 1u) & 0x55555555u) | ((value & 0x55555555u) << 1u);
        value = ((value >> 2u) & 0x33333333u) | ((value & 0x33333333u) << 2u);
        value = ((value >> 4u) & 0x0F0F0F0Fu) | ((value & 0x0F0F0F0Fu) << 4u);
        value = ((value >> 8u) & 0x00FF00FFu) | ((value & 0x00FF00FFu) << 8u);
        c.set_gpr(d.rd, (value >> 16u) | (value << 16u));
        break;
    }
    case K::Wsbh: c.set_gpr(d.rd, ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u)); break;
    case K::Wsbw: c.set_gpr(d.rd, ((b & 255u) << 24u) | ((b & 0xFF00u) << 8u) | ((b >> 8u) & 0xFF00u) | (b >> 24u)); break;
    case K::Lw: c.set_gpr(d.rt, m.load32(address)); break;
    case K::Lwl: c.set_gpr(d.rt, m.aot_load_word_left(address, b)); break;
    case K::Lwr: c.set_gpr(d.rt, m.aot_load_word_right(address, b)); break;
    case K::Ll:
        if (d.rt != 0) c.set_gpr(d.rt, m.load32(address));
        c.load_linked = true;
        break;
    case K::Sc:
        // Match PPSSPP's saved LLBit semantics, including retaining the bit
        // after SC. It stores no reservation address in its state format.
        if (c.load_linked) m.store32(address, b);
        c.set_gpr(d.rt, c.load_linked ? 1u : 0u);
        break;
    case K::Sw: m.store32(address, b); break;
    case K::Swl: m.aot_store_word_left(address, b); break;
    case K::Swr: m.aot_store_word_right(address, b); break;
    case K::Lh: c.set_gpr(d.rt, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(m.load16(address))))); break;
    case K::Lhu: c.set_gpr(d.rt, m.load16(address)); break;
    case K::Sh: m.store16(address, static_cast<std::uint16_t>(b)); break;
    case K::Lb: c.set_gpr(d.rt, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(m.load8(address))))); break;
    case K::Lbu: c.set_gpr(d.rt, m.load8(address)); break;
    case K::Sb: m.store8(address, static_cast<std::uint8_t>(b)); break;
    case K::Lwc1: c.set_fpr_bits(d.rt, m.load32(address)); break;
    case K::Swc1: m.store32(address, c.fpr_bits(d.rt)); break;
    case K::Mfhi: c.set_gpr(d.rd, c.hi); break;
    case K::Mflo: c.set_gpr(d.rd, c.lo); break;
    case K::Mthi: c.hi = a; break;
    case K::Mtlo: c.lo = a; break;
    case K::Mult: case K::Multu: {
        const auto product = d.kind == K::Mult ? static_cast<std::uint64_t>(static_cast<std::int64_t>(sa) * sb)
                                              : static_cast<std::uint64_t>(a) * b;
        c.lo = static_cast<std::uint32_t>(product);
        c.hi = static_cast<std::uint32_t>(product >> 32u);
        break;
    }
    case K::Div:
        if (b == 0) { c.lo = sa >= 0 ? 0xFFFFFFFFu : 1u; c.hi = a; }
        else if (a == 0x80000000u && sb == -1) { c.lo = a; c.hi = 0; }
        else { c.lo = static_cast<std::uint32_t>(sa / sb); c.hi = static_cast<std::uint32_t>(sa % sb); }
        break;
    case K::Divu: c.lo = b == 0 ? 0xFFFFFFFFu : a / b; c.hi = b == 0 ? a : a % b; break;
    case K::Mfc1: c.set_gpr(d.rt, c.fpr_bits(d.rd)); break;
    case K::Mtc1: c.set_fpr_bits(d.rd, b); break;
    case K::Cfc1:
        if (d.rd == 31u) c.set_gpr(d.rt, c.fcr31);
        else rt.unsupported(c.pc, d.word, "unsupported CFC1 control register");
        break;
    case K::Ctc1:
        if (d.rd == 31u) c.fcr31 = b & 0x0181FFFFu;
        else rt.unsupported(c.pc, d.word, "unsupported CTC1 control register");
        break;
    case K::AddS: c.fpr[d.sa] = c.fpr[d.rd] + c.fpr[d.rt]; break;
    case K::SubS: c.fpr[d.sa] = c.fpr[d.rd] - c.fpr[d.rt]; break;
    case K::MulS: {
        const auto fs = c.fpr[d.rd], ft = c.fpr[d.rt];
        if ((std::isinf(fs) && ft == 0.0f) || (std::isinf(ft) && fs == 0.0f)) c.set_fpr_bits(d.sa, 0x7FC00000u);
        else c.fpr[d.sa] = fs * ft;
        break;
    }
    case K::DivS: c.fpr[d.sa] = c.fpr[d.rd] / c.fpr[d.rt]; break;
    case K::SqrtS: c.fpr[d.sa] = std::sqrt(c.fpr[d.rd]); break;
    case K::AbsS: c.set_fpr_bits(d.sa, c.fpr_bits(d.rd) & 0x7FFFFFFFu); break;
    case K::MovS: c.set_fpr_bits(d.sa, c.fpr_bits(d.rd)); break;
    case K::NegS: c.set_fpr_bits(d.sa, c.fpr_bits(d.rd) ^ 0x80000000u); break;
    case K::RoundWS: c.set_fpr_bits(d.sa, c.fpu_float_to_word(c.fpr[d.rd], 0u)); break;
    case K::TruncWS: c.set_fpr_bits(d.sa, c.fpu_float_to_word(c.fpr[d.rd], 1u)); break;
    case K::CeilWS: c.set_fpr_bits(d.sa, c.fpu_float_to_word(c.fpr[d.rd], 2u)); break;
    case K::FloorWS: c.set_fpr_bits(d.sa, c.fpu_float_to_word(c.fpr[d.rd], 3u)); break;
    case K::CvtWS: c.set_fpr_bits(d.sa, c.fpu_float_to_word(c.fpr[d.rd], 4u)); break;
    case K::CvtSW: c.fpr[d.sa] = static_cast<float>(static_cast<std::int32_t>(c.fpr_bits(d.rd))); break;
    case K::FpuCompare: {
        const auto fs = c.fpr[d.rd], ft = c.fpr[d.rt];
        const auto test = d.word & 7u;
        const bool unordered = std::isnan(fs) || std::isnan(ft);
        c.set_fpu_condition(((test & 1u) != 0 && unordered) || ((test & 2u) != 0 && fs == ft) || ((test & 4u) != 0 && fs < ft));
        break;
    }
    default:
        if (!vfpu(d, c, m)) rt.unsupported(c.pc, d.word, "instruction not implemented by interpreter");
        break;
    }
}
} // namespace

void interpret_instruction(Runtime &rt, AllegrexContext &c) {
    const auto pc = c.pc;
    if ((pc & 3u) != 0) { rt.unsupported(pc, 0, "unaligned interpreter PC"); return; }
    const auto d = decode_allegrex(rt.memory().load32(pc));
    const bool slot = c.in_delay_slot;
    const auto next = slot ? c.next_pc : pc + 4u;
    if (d.has_delay_slot()) {
        if (slot) { rt.unsupported(pc, d.word, "control flow in delay slot"); return; }
        auto target = pc + 8u;
        bool taken = true;
        if (d.kind == K::J || d.kind == K::Jal) {
            target = ((pc + 4u) & 0xF0000000u) | (d.target << 2u);
            if (d.kind == K::Jal) c.set_gpr(31, pc + 8u);
        } else if (d.kind == K::Jr || d.kind == K::Jalr) {
            target = c.gpr[d.rs];
            if (d.kind == K::Jalr) c.set_gpr(d.rd, pc + 8u);
        } else {
            taken = condition(d, c);
            if (taken) target = pc + 4u + static_cast<std::uint32_t>(static_cast<std::int32_t>(d.immediate) * 4);
            if (d.kind == K::Bltzal || d.kind == K::Bgezal || d.kind == K::Bltzall || d.kind == K::Bgezall)
                c.set_gpr(31, pc + 8u);
        }
        if (!taken && likely(d.kind)) { c.pc = pc + 8u; return; }
        c.next_pc = target;
        c.in_delay_slot = true;
        c.pc = pc + 4u;
    } else if (d.kind == K::Syscall) {
        const auto owner = capture_runtime_execution_context();
        c.in_delay_slot = false;
        c.next_pc = 0;
        rt.invoke_syscall(c, (d.word >> 6u) & 0xFFFFFu);
        if (!rt.stopped() && runtime_execution_context_matches(owner) && c.pc == pc) c.pc = next;
    } else {
        regular(rt, c, d);
        if (!rt.stopped()) { c.pc = next; c.in_delay_slot = false; c.next_pc = 0; }
    }
}
} // namespace psprecomp
