#include <stdio.h>
#include "mcp.h"
#include "cJSON.h"
#include "generated_function_signatures.h"

#ifdef __cplusplus
extern "C" {
#endif

extern cJSON* get_all_function_signatures_json();
int mcp_serve() {
    char buffer[4096] = {0};
    cJSON *json = NULL;
    cJSON *response = NULL;
    int result = 0;
    
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
    
    printf("json: %s\n", cJSON_PrintUnformatted(get_all_function_signatures_json()));
    // Process JSON data here
    response = bridge(json);
    printf("%s\n", cJSON_Print(response));
    // Clean up resources
    cJSON_Delete(json);
    cJSON_Delete(response);
    return result;
}

#ifdef __cplusplus
}
#endif