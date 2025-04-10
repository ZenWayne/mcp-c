#include "export_macro.h"
#include "cJSON.h"
#include "file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generated_func.h"

#ifdef __cplusplus
extern "C" {
#endif

EXPORT_AS(tools, list)
cJSON* tools_list() {
    int i = 0;
    return get_all_function_signatures_json();
}

// 获取人员基本信息
cJSON* real_get_person(int id) {
    // 获取人员基本信息
    return cJSON_CreateObject(); //这里只是简单示例
}

// 保存人员基本信息
cJSON* real_save_person(person* person_obj) {
    return cJSON_CreateObject(); //这里只是简单示例
}

// 这里实现我们需要导出的函数
EXPORT_AS(get_color)
cJSON* get_color(COLOR c) {
    cJSON* json = cJSON_CreateObject();
    if (json) {
        const char* colorName = "UNKNOWN";
        switch (c) {
            case RED: colorName = "RED"; break;
            case GREEN: colorName = "GREEN"; break;
            case BLUE: colorName = "BLUE"; break;
        }
        cJSON_AddStringToObject(json, "color", colorName);
    }
    return json;
}

EXPORT_AS(get_person_info)
cJSON* get_person_info(person* p, int id) {
    if (!p) return NULL;
    
    cJSON* json = cJSON_CreateObject();
    if (json) {
        cJSON_AddBoolToObject(json, "isMale", p->isMale);
        cJSON_AddNumberToObject(json, "age", p->age);
        if (p->name) {
            cJSON_AddStringToObject(json, "name", p->name);
        }
        
        cJSON* clothJson = cJSON_CreateObject();
        if (clothJson) {
            const char* colorName = "UNKNOWN";
            switch (p->wearing_cloths.color) {
                case RED: colorName = "RED"; break;
                case GREEN: colorName = "GREEN"; break;
                case BLUE: colorName = "BLUE"; break;
            }
            cJSON_AddStringToObject(clothJson, "color", colorName);
            cJSON_AddNumberToObject(clothJson, "size", p->wearing_cloths.size);
            cJSON_AddItemToObject(json, "wearing_cloths", clothJson);
        }
    }
    int a = 1;
    return json;
}

#ifdef __cplusplus
}
#endif