// 函数签名自动生成，请勿修改
#include "string.h"
#include "function_signature.h"

#include "cJSON.h"
static struct ParameterInfo real_get_person_params[] = {
    {"int", "id"}
};

struct FunctionSignature real_get_person_signature = {
    "cJSON *",
    1,
    real_get_person_params
};

static struct ParameterInfo save_person_params[] = {
    {"person *", "person_obj"}
};

struct FunctionSignature save_person_signature = {
    "cJSON *",
    1,
    save_person_params
};

// 定义函数签名字典
static struct {
    const char* name;
    struct FunctionSignature* signature;
} function_signature_map[] = {
    {"real_get_person", &real_get_person_signature},
    {"save_person", &save_person_signature},
    {NULL, NULL}  // 结束标记
};

cJSON* get_all_function_signatures_json() {
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    // 遍历所有函数签名
    for (int i = 0; function_signature_map[i].name != NULL; i++) {
        const char* func_name = function_signature_map[i].name;
        struct FunctionSignature* sig = function_signature_map[i].signature;

        cJSON* func_obj = cJSON_CreateObject();
        if (func_obj == NULL) {
            cJSON_Delete(root);
            return NULL;
        }

        // 添加返回类型
        cJSON_AddStringToObject(func_obj, "return_type", sig->returnType);
        
        // 添加参数数量
        cJSON_AddNumberToObject(func_obj, "param_count", sig->parameterCount);
        
        // 创建参数数组
        cJSON* params = cJSON_CreateArray();
        if (params == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
        
        // 添加每个参数的信息
        for (int j = 0; j < sig->parameterCount; j++) {
            cJSON* param = cJSON_CreateObject();
            if (param == NULL) {
                cJSON_Delete(root);
                return NULL;
            }
            
            cJSON_AddStringToObject(param, "type", sig->params[j].type);
            cJSON_AddStringToObject(param, "name", sig->params[j].name);
            cJSON_AddItemToArray(params, param);
        }
        
        cJSON_AddItemToObject(func_obj, "params", params);
        cJSON_AddItemToObject(root, func_name, func_obj);
    }

    return root;
}

#ifdef __cplusplus
}
#endif
