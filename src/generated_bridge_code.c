// 桥接代码 (自动生成，请勿修改)
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // For NULL, potentially error messages
#include "file.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- External Function Declarations --- 
extern cJSON * tools_list();
extern cJSON * get_color(COLOR c);
extern cJSON * get_person_info(person * p, int id);

// Parser for enum COLOR
static inline COLOR parse_COLOR(cJSON *json) {
    if (!json) return (COLOR)0; // Or a default/error value
    if (cJSON_IsString(json)) {
        const char* str = json->valuestring;
        if (strcmp(str, "RED") == 0) {
            return RED;
        }
        else if (strcmp(str, "GREEN") == 0) {
            return GREEN;
        }
        else if (strcmp(str, "BLUE") == 0) {
            return BLUE;
        }
        else {
            // Handle error: unknown string value
            return (COLOR)0; // Default/error value
        }
    } else if (cJSON_IsNumber(json)) {
        return (COLOR)json->valueint;
    } else {
         // Handle error: unexpected JSON type
        return (COLOR)0; // Default/error value
    }
}

// Parser for struct cloth
static inline struct cloth* parse_cloth(cJSON *json) {
    if (!json || !cJSON_IsObject(json)) return NULL;
    struct cloth* obj = (struct cloth*)malloc(sizeof(struct cloth));
    if (!obj) return NULL;
    memset(obj, 0, sizeof(struct cloth)); // Initialize memory

    cJSON* field_json = NULL;
    // Field: color
    field_json = cJSON_GetObjectItem(json, "color");
    if (field_json && !cJSON_IsNull(field_json)) { // Check field exists and is not null
        // Warning: Cannot parse non-exported enum field 'color'
    } else {
        // Field missing or null in JSON, obj->color remains initialized (likely 0/NULL)
    }
    // Field: size
    field_json = cJSON_GetObjectItem(json, "size");
    if (field_json && !cJSON_IsNull(field_json)) { // Check field exists and is not null
        if (cJSON_IsNumber(field_json)) {
            obj->size = field_json->valueint;
        }
    } else {
        // Field missing or null in JSON, obj->size remains initialized (likely 0/NULL)
    }

    return obj;
}

// Parser for struct person
static inline struct person* parse_person(cJSON *json) {
    if (!json || !cJSON_IsObject(json)) return NULL;
    struct person* obj = (struct person*)malloc(sizeof(struct person));
    if (!obj) return NULL;
    memset(obj, 0, sizeof(struct person)); // Initialize memory

    cJSON* field_json = NULL;
    // Field: isMale
    field_json = cJSON_GetObjectItem(json, "isMale");
    if (field_json && !cJSON_IsNull(field_json)) { // Check field exists and is not null
        if (cJSON_IsBool(field_json)) {
            obj->isMale = cJSON_IsTrue(field_json);
        }
    } else {
        // Field missing or null in JSON, obj->isMale remains initialized (likely 0/NULL)
    }
    // Field: age
    field_json = cJSON_GetObjectItem(json, "age");
    if (field_json && !cJSON_IsNull(field_json)) { // Check field exists and is not null
        if (cJSON_IsNumber(field_json)) {
            obj->age = field_json->valueint;
        }
    } else {
        // Field missing or null in JSON, obj->age remains initialized (likely 0/NULL)
    }
    // Field: name
    field_json = cJSON_GetObjectItem(json, "name");
    if (field_json && !cJSON_IsNull(field_json)) { // Check field exists and is not null
        if (cJSON_IsString(field_json)) {
            obj->name = strdup(field_json->valuestring);
           // TODO: Remember to free this string later!
        }
    } else {
        // Field missing or null in JSON, obj->name remains initialized (likely 0/NULL)
    }
    // Field: wearing_cloths
    field_json = cJSON_GetObjectItem(json, "wearing_cloths");
    if (field_json && !cJSON_IsNull(field_json)) { // Check field exists and is not null
        struct cloth* temp_wearing_cloths = parse_cloth(field_json);
        if (temp_wearing_cloths) {
            obj->wearing_cloths = *temp_wearing_cloths; // Copy value
            free(temp_wearing_cloths); // Free temporary parsed object
        }
    } else {
        // Field missing or null in JSON, obj->wearing_cloths remains initialized (likely 0/NULL)
    }

    return obj;
}

// --- Main Bridge Function --- 
cJSON* bridge(cJSON* input_json) {
    if (!input_json) return NULL;
    cJSON* func_item = cJSON_GetObjectItem(input_json, "func");
    cJSON* param_item = cJSON_GetObjectItem(input_json, "param");

    if (!func_item || !cJSON_IsString(func_item) || !param_item || !cJSON_IsObject(param_item)) {
        // Invalid input format
        // TODO: Return error JSON?
        return NULL;
    }

    const char* func_name = func_item->valuestring;
    cJSON* result = NULL;

    // Memory to free after function call
    #define MAX_ALLOCS 10 // Adjust as needed
    void* allocations[MAX_ALLOCS];
    int alloc_count = 0;

    if (0) {
    } else if (strcmp(func_name, "tools/list") == 0) {

        // Parse parameters
        cJSON* p_json = NULL;

        // Call original function
        result = tools_list();
    } else if (strcmp(func_name, "get_color") == 0) {
        COLOR p_c = {0}; // Initialize

        // Parse parameters
        cJSON* p_json = NULL;
        p_json = cJSON_GetObjectItem(param_item, "c");
        if (p_json && !cJSON_IsNull(p_json)&& alloc_count < MAX_ALLOCS) {
            p_c = parse_COLOR(p_json);
        } else {
           // Parameter c missing or null
           fprintf(stderr, "Parameter c missing or null");
        }

        // Call original function
        result = get_color(p_c);
    } else if (strcmp(func_name, "get_person_info") == 0) {
        person * p_p = {0}; // Initialize
        int p_id = {0}; // Initialize

        // Parse parameters
        cJSON* p_json = NULL;
        p_json = cJSON_GetObjectItem(param_item, "p");
        if (p_json && !cJSON_IsNull(p_json)&& alloc_count < MAX_ALLOCS) {
            p_p = parse_person(p_json);
           if (p_p && alloc_count < MAX_ALLOCS) allocations[alloc_count++] = p_p;
        } else {
           // Parameter p missing or null
           fprintf(stderr, "Parameter p missing or null");
        }
        p_json = cJSON_GetObjectItem(param_item, "id");
        if (p_json && !cJSON_IsNull(p_json)&& alloc_count < MAX_ALLOCS) {
            if (cJSON_IsNumber(p_json)) p_id = p_json->valueint;
        } else {
           // Parameter id missing or null
           fprintf(stderr, "Parameter id missing or null");
        }

        // Call original function
        result = get_person_info(p_p, p_id);
    }

        // Free allocated memory for parameters
    for (int i = 0; i < alloc_count; ++i) {
        if (allocations[i]) free(allocations[i]);
    }

    return result;
}


#ifdef __cplusplus
} // extern "C"
#endif
