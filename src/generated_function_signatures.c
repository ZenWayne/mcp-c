// 函数签名 JSON 生成代码 (自动生成，请勿修改)
#include "cJSON.h"
#include "string.h" // For strcmp in potential enum parsing fallback

#include "file.h"

#ifdef __cplusplus
extern "C" {
#endif

cJSON* get_all_function_signatures_json() {
    cJSON* root = cJSON_CreateObject();
    if (!root) return NULL;

    // --- $defs --- 
    cJSON* defs = cJSON_CreateObject();
    if (!defs) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "$defs", defs);

    // --- tools --- 
    cJSON* tools = cJSON_CreateArray();
    if (!tools) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "tools", tools);

    return root;
}

#ifdef __cplusplus
} // extern "C"
#endif
