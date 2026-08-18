#ifndef FILE_H
#define FILE_H
#include "export_macro.h"
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EXPORT_AS(obj) obj {
    int placeholder;
} obj;

cJSON *get_flag(char *auth_token DES("Authentication token")) DES("Returns a flag for authenticated users");

cJSON *get_scores() DES("Returns the current scores");

#ifdef __cplusplus
}
#endif
#endif
