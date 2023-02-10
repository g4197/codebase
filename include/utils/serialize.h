#ifndef SERIALIZE_H_
#define SERIALIZE_H_

#include <algorithm>
#include <string>

#include "slice.h"

// \param buf The buffer to serialize to.
// \param args The object to serialize. Supported types: Slice, std::string, POD types.
// \return The size of the serialized object.

namespace Serializer {
template<bool multi_string, class T0, class... Ts>
uint64_t serialize_impl(char *buf, T0 t0, Ts... args) {
    uint64_t size = 0;
    if constexpr (std::is_pointer_v<T0>) {
        // not using "false" to avoid ill-formed problem
        static_assert(!sizeof(T0 *), "Pointer is not allowed");
    } else if constexpr (std::is_same_v<T0, Slice> || std::is_same_v<T0, std::string>) {
        if constexpr (multi_string == false) {
            if constexpr (sizeof...(args) > 0) {
                static_assert(!sizeof(T0 *), "Slice or string must be the last argument");
            }
            memcpy(buf, t0.data(), t0.size());
            size += t0.size();
        } else {
            uint64_t sz = t0.size();
            memcpy(buf, &sz, sizeof(uint64_t));
            memcpy(buf + sizeof(uint64_t), t0.data(), t0.size());
            size += sizeof(uint64_t) + t0.size();
        }
    } else if constexpr (std::is_pod_v<T0>) {
        memcpy(buf, &t0, sizeof(T0));
        size += std::max(sizeof(uint64_t), sizeof(T0));  // alignment.
    } else {
        static_assert(!sizeof(T0 *), "Unsupported type");
    }
    if constexpr (sizeof...(args) > 0) {
        size += serialize_impl<multi_string>(buf + size, args...);
    }
    return size;
}

// \param buf The buffer to deserialize from.
// \param size The size of the buffer.
// \param args The object to deserialize. Supported types: Slice, std::string, POD types, pointer of POD types.
// Pointers of POD types will be pointed to the corresponding position in the buffer.
// POD types will be copied from the buffer.
template<bool multi_string, class T0, class... Ts>
void deserialize_impl(const char *buf, uint64_t size, T0 &t0, Ts &...args) {
    uint64_t cur_size = 0;
    if constexpr (std::is_pointer_v<T0>) {
        t0 = (T0)buf;
        cur_size += sizeof(*t0);
    } else if constexpr (std::is_same_v<T0, Slice>) {
        if constexpr (multi_string == false) {
            t0 = Slice(buf, size);
        } else {
            uint64_t sz;
            memcpy(&sz, buf, sizeof(uint64_t));
            t0 = Slice(buf + sizeof(uint64_t), sz);
            cur_size += sizeof(uint64_t) + sz;
        }
    } else if constexpr (std::is_same_v<T0, std::string>) {
        if constexpr (multi_string == false) {
            t0 = std::string(buf, size);
        } else {
            uint64_t sz;
            memcpy(&sz, buf, sizeof(uint64_t));
            t0 = std::string(buf + sizeof(uint64_t), sz);
            cur_size += sizeof(uint64_t) + sz;
        }
    } else if constexpr (std::is_pod_v<T0>) {
        memcpy(&t0, buf, sizeof(T0));
        cur_size += std::max(sizeof(T0), sizeof(uint64_t));
    } else {
        static_assert(!sizeof(T0 *), "Unsupported type");
    }
    if constexpr (sizeof...(args) > 0) {
        deserialize_impl<multi_string>(buf + cur_size, size - cur_size, args...);
    }
}
};  // namespace Serializer

template<class T0, class... Ts>
uint64_t serialize(char *buf, T0 t0, Ts... args) {
    return Serializer::serialize_impl<false>(buf, t0, args...);
}

template<class T0, class... Ts>
void deserialize(const char *buf, uint64_t size, T0 &t0, Ts &...args) {
    Serializer::deserialize_impl<false>(buf, size, t0, args...);
}

template<class T0, class... Ts>
uint64_t m_serialize(char *buf, T0 t0, Ts... args) {
    return Serializer::serialize_impl<true>(buf, t0, args...);
}

template<class T0, class... Ts>
void m_deserialize(const char *buf, uint64_t size, T0 &t0, Ts &...args) {
    Serializer::deserialize_impl<true>(buf, size, t0, args...);
}

#endif  // SERIALIZE_H_