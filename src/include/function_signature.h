#ifndef FUNCTION_SIGNATURE_H
#define FUNCTION_SIGNATURE_H

#ifdef __cplusplus
extern "C" {
#endif

struct ParameterInfo {
    const char* type;
    const char* name;
};

struct FunctionSignature {
    const char* returnType;
    int parameterCount;
    struct ParameterInfo* params;  // 使用指针数组
};

#ifdef __cplusplus
}
#endif

#endif // FUNCTION_SIGNATURE_H