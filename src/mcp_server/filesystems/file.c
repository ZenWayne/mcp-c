#include "export_macro.h"
#include "cJSON.h"
#include "file.h"

#ifdef __cplusplus
extern "C" {
#endif

// 获取人员基本信息
EXPORT
cJSON* real_get_person(int id) {
    // 获取人员基本信息
    return cJSON_CreateObject(); //这里只是简单示例
}

// 保存人员基本信息
EXPORT_AS(save_person)
cJSON* real_save_person(person* person_obj) {
    return cJSON_CreateObject(); //这里只是简单示例
}

#ifdef __cplusplus
}
#endif