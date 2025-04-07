// 桥接代码自动生成，请勿修改
#include "cJSON.h"
#include "string.h"
#include "stdlib.h"
#include "function_signature.h"
#include "filesystems\file.h"

#ifdef __cplusplus
extern "C" {
#endif

// 外部函数声明
extern cJSON * real_get_person(int id);
extern cJSON * real_save_person(person * person_obj);

inline person* parse_person(cJSON *json) {
    person* obj = (person*)malloc(sizeof(person));
    obj->name = cJSON_GetObjectItem(json, "name")->valuestring;
    obj->age = cJSON_GetObjectItem(json, "age")->valueint;
    obj->height = cJSON_GetObjectItem(json, "height")->valueint;
    return obj;
}

cJSON* bridge(cJSON* input_json) {
    const char* func_name = cJSON_GetObjectItem(input_json, "func")->valuestring;
    cJSON* param_json = cJSON_GetObjectItem(input_json, "param");
    if (strcmp(func_name, "real_get_person") == 0) {
        int id = cJSON_GetObjectItem(param_json, "id")->valueint;
        cJSON* result = real_get_person(id);
        return result;
    }
    if (strcmp(func_name, "real_save_person") == 0) {
        person* person_obj = parse_person(cJSON_GetObjectItem(param_json, "person_obj"));
        cJSON* result = real_save_person(person_obj);
        free(person_obj);
        return result;
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif
