#ifndef TYPE_H_
#define TYPE_H_

#include <string>
#include <type_traits>

template<typename T>
inline std::string type_name() {
    typedef typename std::remove_reference<T>::type TR;
    std::unique_ptr<char, void (*)(void *)> own(
#ifndef _MSC_VER
        abi::__cxa_demangle(typeid(TR).name(), nullptr, nullptr, nullptr),
#else
        nullptr,
#endif
        std::free);
    std::string r = own != nullptr ? own.get() : typeid(TR).name();
    if (std::is_const<TR>::value) r += " const";
    if (std::is_volatile<TR>::value) r += " volatile";
    if (std::is_lvalue_reference<T>::value) r += "&";
    else if (std::is_rvalue_reference<T>::value) r += "&&";
    return r;
}

#define IS_SAME(x, y) (std::is_same<decltype(x), decltype(y)>::value)
#define IS_POINTER(x) (std::is_pointer<decltype(x)>::value)

#endif  // TYPE_H_