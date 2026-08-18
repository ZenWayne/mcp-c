#include <stdio.h>
#include <string.h>
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
        //fprintf(stderr, "Error reading from stdin\n");
        return -1;
    }
    //fprintf(stderr, "Got: %s", buffer);
    
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
        cJSON *method = cJSON_GetObjectItemCaseSensitive(json, "method");
        //fprintf(stderr, "Got: %s\n", method->valuestring);
        if (!strncmp(method->valuestring, "notifications/", strlen("notifications/"))) {
            //fprintf(stderr, "Ignoring missing ID for: %s\n", method->valuestring);
            return 1;
        }
        fprintf(stderr, "Invalid or missing request ID\n");
        cJSON_Delete(json);
        return -1;
    }

    // Process JSON data here
    cJSON* response = cJSON_CreateObject();
    
    // Add jsonrpc version
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    // Add ID to response
    cJSON_AddNumberToObject(response, "id", id->valueint);
    result = bridge(json);
    if (result != NULL) {
        if (response != NULL) {
            // Add result to response
            cJSON_AddItemToObject(response, "result", result);
        }
    }else{
        cJSON_AddItemToObject(response, "result", cJSON_CreateObject());
        fprintf(stderr, "result is NULL\n");
    }
    //fprintf(stderr, "Replying with: %s\n", cJSON_PrintUnformatted(response));
    printf("%s\n", cJSON_PrintUnformatted(response));
    fflush(stdout);
    // Clean up resources
    cJSON_Delete(json);
    cJSON_Delete(response);
    return ret;
}

#ifdef __cplusplus
}
#endif