#pragma once
#include "save_state/ppst.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>

namespace sarusaru::save_state {
// Native snapshots are locked to a host/runtime ABI. Dynamic objects and host
// bindings never use object-representation serialization.
template<bool Reading> class NativeArchive {
public:
    static constexpr bool reading = Reading;
    PayloadWriter output;
    std::optional<PayloadReader> input;
    explicit NativeArchive(std::span<const std::uint8_t> bytes = {}) { if constexpr (Reading) input.emplace(bytes); }
    template<class... T> void operator()(T &...value) { (item(value), ...); }
    template<class T> void pod(T &value) {
        static_assert(std::is_trivially_copyable_v<T>);
        if constexpr (Reading) { auto bytes = input->bytes(sizeof(T)); std::memcpy(&value, bytes.data(), sizeof(T)); }
        else output.bytes({reinterpret_cast<const std::uint8_t *>(&value), sizeof(T)});
    }
    template<class T> requires (std::is_arithmetic_v<T> || std::is_enum_v<T>) void item(T &value) {
        if constexpr (std::is_same_v<T, bool>) {
            if constexpr (Reading) value = input->boolean(); else output.boolean(value);
        } else pod(value);
    }
    void item(std::string &value) {
        if constexpr (Reading) value = input->string(); else output.string(value);
    }
    template<class T, std::size_t N> void item(std::array<T,N> &values) { for (auto &v : values) item(v); }
    template<class A, class B> void item(std::pair<A,B> &value) { (*this)(value.first, value.second); }
    template<class T> void item(std::optional<T> &value) {
        bool has = value.has_value(); item(has);
        if constexpr (Reading) { if (has) value.emplace(); else value.reset(); }
        if (has) item(*value);
    }
    template<class T> void item(std::unique_ptr<T> &value) {
        bool has = bool(value); item(has);
        if constexpr (Reading) { if (has) value = std::make_unique<T>(); else value.reset(); }
        if (has) item(*value);
    }
    template<class T> void sequence(T &values) {
        if constexpr (!Reading) {
            if (values.size()>64u*1024u*1024u || (sizeof(typename T::value_type)>1 && values.size()>1'000'000u))
                throw FormatError("Native state collection exceeds its size limit");
        }
        std::uint32_t size = static_cast<std::uint32_t>(values.size());
        item(size);
        if constexpr (Reading) {
            if (size > input->remaining() || size > 64u * 1024u * 1024u ||
                (sizeof(typename T::value_type) > 1 && size > 1'000'000u)) throw FormatError("Invalid native state collection");
            values.clear(); values.resize(size);
        }
        if constexpr (sizeof(typename T::value_type) == 1 && requires { values.data(); }) {
            if constexpr (Reading) { auto data = input->bytes(size); if (size != 0) std::memcpy(values.data(), data.data(), size); }
            else output.bytes({reinterpret_cast<const std::uint8_t *>(values.data()), size});
        } else for (auto &v : values) item(v);
    }
    template<class T> void item(std::vector<T> &v) { sequence(v); }
    template<class T> void item(std::deque<T> &v) { sequence(v); }
    template<class K, class V> void item(std::map<K,V> &values) {
        if constexpr (!Reading) if (values.size()>65536u) throw FormatError("Native state map exceeds its size limit");
        std::uint32_t size = static_cast<std::uint32_t>(values.size()); item(size);
        if constexpr (Reading) {
            if (size > 65536u || size > input->remaining()) throw FormatError("Invalid native state map");
            values.clear();
            for (std::uint32_t i=0;i<size;++i) { K k{}; V v{}; (*this)(k,v); if (!values.emplace(std::move(k),std::move(v)).second) throw FormatError("Duplicate native state key"); }
        } else for (auto &[k,v] : values) { auto key=k; (*this)(key,v); }
    }
    template<class K> void item(std::set<K> &values) {
        std::vector<K> list(values.begin(),values.end()); item(list);
        if constexpr (Reading) values = std::set<K>(list.begin(),list.end());
    }
    template<class T> requires (!std::is_arithmetic_v<T> && !std::is_enum_v<T>) void item(T &value) { fields(*this,value); }
};
}
