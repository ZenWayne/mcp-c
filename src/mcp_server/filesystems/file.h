#include "export_macro.h"
#include <stdbool.h>

typedef enum EXPORT_AS(color) {
    RED,
    GREEN,
    BLUE
} COLOR;

typedef struct EXPORT_AS(cloth) cloth {
    enum COLOR color DES("color of the cloth");
    int size;
} cloth;

typedef struct EXPORT_AS(person) person {
    bool isMale DES("whether is male or not male is true");
    int age DES("age of the person");
    char* name;
    cloth wearing_cloths;
} person;
