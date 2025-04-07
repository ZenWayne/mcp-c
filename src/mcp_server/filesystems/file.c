#include "cJSON.h"
#include "export_macro.h"
#include "file.h"

#ifdef __cplusplus
extern "C" {
#endif

// 获取人员基本信息
EXPORT
cJSON* real_get_person(int id) {
    cJSON* person = cJSON_CreateObject();
    cJSON_AddStringToObject(person, "name", "John Doe");
    cJSON_AddNumberToObject(person, "age", 30);
    cJSON_AddNumberToObject(person, "height", 180);
    return person;
}

// 保存人员基本信息
EXPORT_AS(save_person)
cJSON* real_save_person(person* person_obj) {
    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddStringToObject(result, "message", "Person saved successfully");
    return result;
}

#ifdef __cplusplus
}
#endif