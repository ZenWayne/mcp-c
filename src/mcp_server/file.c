#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h> // For strcmp

#include "cJSON.h"  // Make sure to include the cJSON header
#include "export_macro.h"
#include "file.h"

struct __attribute__((__packed__)) auth_setting {
    char token[12];
    uint8_t authd;
    uint8_t cookie;
};

EXPORT_AS(get_flag)
cJSON *get_flag(char *auth_token) {
    struct auth_setting *as = malloc(sizeof(struct auth_setting));
    memset(as, 0, sizeof(struct auth_setting));
    char fstring[80] = "flag: ";
    cJSON *res = cJSON_CreateObject();
    cJSON *rv = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON_AddStringToObject(rv, "type", "text");
    //printf("Len of in: %lu\n", strlen(auth_token));
    strcpy(as->token, auth_token);
    if (as->cookie) {
        fprintf(stderr, "the stack cookie has been tampered with...\n");
        raise(SIGSEGV);
    }
    if (getenv("MCP_AUTH_TOKEN") == NULL) {
        fprintf(stderr, "NULL ENV!\n");
    }
    if (getenv("MCP_FLAG") == NULL) {
        fprintf(stderr, "NULL FLAG ENV!\n");
    }
    if (!strcmp(as->token, getenv("MCP_AUTH_TOKEN"))) {
        as->authd = 1;
    }
    if (as->authd) {
        cJSON_AddStringToObject(rv, "text", strcat(fstring, getenv("MCP_FLAG")));
    } else {
        cJSON_AddStringToObject(rv, "text", "error: invalid token");
    }
    cJSON_AddItemToArray(content, rv);
    cJSON_AddItemToObject(res, "content", content);
    free(as);
    return res;
}

EXPORT_AS(get_scores)
cJSON *get_scores() {
    cJSON *res = cJSON_CreateObject();
    cJSON *rv = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON_AddStringToObject(rv, "type", "text");
    // TODO!
    cJSON_AddStringToObject(rv, "text", "Kimos: 104\n");
    cJSON_AddItemToArray(content, rv);
    cJSON_AddItemToObject(res, "content", content);
    //printf("Hello from get_scores()\n");
    return res;
}



