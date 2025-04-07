#ifndef EXPORT_MACRO_H
#define EXPORT_MACRO_H


// #define EXPORT_FUNCTION __attribute__((annotate("EXPORT_FUNCTION")))
// #define EXPORT_FUNCTION_AS(name) __attribute__((annotate("EXPORT_FUNCTION_AS(" name ")")))
// #define EXPORT_AS(x) __attribute__((annotate("EXPORT_AS=" #x)))

#ifdef _MSC_VER
    // MSVC 编译器
    #ifdef BUILDING_DLL
        #define EXPORT __declspec(dllexport)
        #define EXPORT_AS(x) __declspec(dllexport)
    #else
        #define EXPORT 
        #define EXPORT_AS(x) 
#endif

#else
    // Clang 编译器
    #define EXPORT __attribute__((annotate("EXPORT")))
    #define EXPORT_AS(x) __attribute__((annotate("EXPORT_AS=" #x)))
#endif

#endif // EXPORT_MACRO_H
