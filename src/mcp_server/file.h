#ifndef FILE_H
#define FILE_H
#include "export_macro.h"
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum EXPORT COLOR {
    RED,
    GREEN,
    BLUE
} COLOR;

typedef struct EXPORT_AS(cloth) cloth {
    enum COLOR color DES("color of the cloth");
    int size;
} cloth;

typedef struct EXPORT person {
    bool isMale DES("whether is male or not male is true");
    int age DES("age of the person");
    char* name;
    cloth wearing_cloths;
} person;

cJSON* get_person_info(person* p);
#ifdef __cplusplus
}
#endif
#endif
