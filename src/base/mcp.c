#include <stdio.h>
#include "export_macro.h"
#include "mcp.h"
#include "generated_func.h"

#ifdef __cplusplus
extern "C" {
#endif

int mcp_serve() {
    char buffer[4096] = {0};
    cJSON *json = NULL;
    cJSON *id = NULL;
    cJSON *result = NULL;
    int ret = 2;
    
    // Read data from standard input
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        fprintf(stderr, "Error reading from stdin\n");
        return -1;
    }
    
    // Parse JSON data
    json = cJSON_Parse(buffer);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "JSON parsing error: %s\n", error_ptr);
        }
        return -1;
    }

    // Get request ID
    id = cJSON_GetObjectItemCaseSensitive(json, "id");
    if (id == NULL || !cJSON_IsNumber(id)) {
        fprintf(stderr, "Invalid or missing request ID\n");
        cJSON_Delete(json);
        return -1;
    }

    // Process JSON data here
    cJSON* response = cJSON_CreateObject();
    result = bridge(json);
    if (result != NULL) {
        if (response != NULL) {
            // Add result to response
            cJSON_AddItemToObject(response, "result", result);
        }
    }else{
        cJSON_AddStringToObject(response, "result", cJSON_CreateObject());
        printf("result is NULL\n");
    }
    // Add ID to response
    cJSON_AddNumberToObject(response, "id", id->valueint);
    // Add jsonrpc version
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    printf("%s\n", cJSON_Print(response));
    // Clean up resources
    cJSON_Delete(json);
    cJSON_Delete(response);
    return ret;
}

#ifdef __cplusplus
}
#endif