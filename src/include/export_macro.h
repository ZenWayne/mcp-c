#ifndef EXPORT_MACRO_H
#define EXPORT_MACRO_H


// #define EXPORT_FUNCTION __attribute__((annotate("EXPORT_FUNCTION")))
// #define EXPORT_FUNCTION_AS(name) __attribute__((annotate("EXPORT_FUNCTION_AS(" name ")")))
// #define EXPORT_AS(x) __attribute__((annotate("EXPORT_AS=" #x)))

#define _CONCAT_HELPER(a, b) a##b
#define _CONCAT(a, b) _CONCAT_HELPER(a, b)

#ifdef _MSC_VER
    // MSVC 编译器
    #ifdef BUILDING_DLL
        #define EXPORT __declspec(dllexport)
        // 在 MSVC 中，我们只使用 dllexport，但在注释中保留原始值以便工具可以读取
        #define _EXPORT_AS1(x) #x
        #define _EXPORT_AS2(x, y) #x "/" #y
        #define _EXPORT_AS3(x, y, z) #x "/" #y "/" #z
        #define _EXPORT_AS4(x, y, z, w) #x "/" #y "/" #z "/" #w
        #define _EXPORT_AS5(x, y, z, w, v) #x "/" #y "/" #z "/" #w "/" #v
        #define _GET_EXPORT_AS_MACRO(_1,_2,_3,_4,_5,NAME,...) NAME
        #define EXPORT_AS(...) __declspec(dllexport) /* EXPORT_AS=_GET_EXPORT_AS_MACRO(__VA_ARGS__, _EXPORT_AS5, _EXPORT_AS4, _EXPORT_AS3, _EXPORT_AS2, _EXPORT_AS1)(__VA_ARGS__) */
        
        #define DES(x) __declspec(dllexport)
    #else
        #define EXPORT 
        // 空定义版本
        #define _EXPORT_AS1(x) #x
        #define _EXPORT_AS2(x, y) #x "/" #y
        #define _EXPORT_AS3(x, y, z) #x "/" #y "/" #z
        #define _EXPORT_AS4(x, y, z, w) #x "/" #y "/" #z "/" #w
        #define _EXPORT_AS5(x, y, z, w, v) #x "/" #y "/" #z "/" #w "/" #v
        #define _GET_EXPORT_AS_MACRO(_1,_2,_3,_4,_5,NAME,...) NAME
        #define EXPORT_AS(...) /* EXPORT_AS=_GET_EXPORT_AS_MACRO(__VA_ARGS__, _EXPORT_AS5, _EXPORT_AS4, _EXPORT_AS3, _EXPORT_AS2, _EXPORT_AS1)(__VA_ARGS__) */
        
        #define DES(x) 
    #endif
#else
    // Clang 编译器
    #define EXPORT __attribute__((annotate("EXPORT")))
    // 支持任意数量参数连接的版本（最多支持5个参数）
    #define _EXPORT_AS1(x) #x
    #define _EXPORT_AS2(x, y) #x "/" #y
    #define _EXPORT_AS3(x, y, z) #x "/" #y "/" #z
    #define _EXPORT_AS4(x, y, z, w) #x "/" #y "/" #z "/" #w
    #define _EXPORT_AS5(x, y, z, w, v) #x "/" #y "/" #z "/" #w "/" #v
    #define _GET_EXPORT_AS_MACRO(_1,_2,_3,_4,_5,NAME,...) NAME
    #define EXPORT_AS(...) __attribute__((annotate("EXPORT_AS=" _GET_EXPORT_AS_MACRO(__VA_ARGS__, _EXPORT_AS5, _EXPORT_AS4, _EXPORT_AS3, _EXPORT_AS2, _EXPORT_AS1)(__VA_ARGS__))))
    
    #define DES(x) __attribute__((annotate("DESCRIPTION=" #x)))
#endif

#endif // EXPORT_MACRO_H
