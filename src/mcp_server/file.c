#include <stdio.h>
#include <stdbool.h>
#include <string.h> // For strcmp

#include "cJSON.h"  // Make sure to include the cJSON header
#include "export_macro.h"
#include "file.h"

EXPORT_AS(get_person_info)
cJSON* get_person_info(person* p) {
    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "message", "Hello, World!");
    printf("p->name: %s\n", p->name);
    printf("p->age: %d\n", p->age);
    printf("p->isMale: %d\n", p->isMale);
    printf("p->wearing_cloths.color: %d\n", p->wearing_cloths.color);
    printf("p->wearing_cloths.size: %d\n", p->wearing_cloths.size);
    return result;
}



