#include <stdio.h>
#include <stdbool.h>
#include <string.h> // For strcmp
#include "cJSON.h"  // Make sure to include the cJSON header
#include "export_macro.h"
#include "generated_func.h"
#include "base_func.h"
// Assume EXPORT_AS is defined in a header provided by the mcp-c framework
// If not, you might need to include the specific header file here.
// #include "mcp_export.h" // Or similar, depending on the framework


// --- Handler for the "initialize" method ---

/**
 * @brief Handles the 'initialize' JSON-RPC request.
 * Constructs the server's response containing its capabilities and info.
 * Exported as the handler for the "initialize" method via EXPORT_AS.
 *
 * @param params The cJSON object containing the parameters sent by the client.
 * @param request_id The ID from the incoming JSON-RPC request.
 *
 * @return cJSON* A pointer to a cJSON object representing the 'result' field.
 * Returns NULL on failure. Framework manages deletion.
 */
EXPORT_AS(initialize)
cJSON* initialize(char* protocolVersion, struct capabilities* capabilities, struct client_info* clientInfo) {
    // 创建响应对象
    cJSON* result = cJSON_CreateObject();
    if (!result) {
        return NULL;
    }

    fprintf(stderr, "protocolVersion: %s\n", protocolVersion);
    fprintf(stderr, "capabilities.roots.listChanged: %d\n", capabilities->roots.listChanged);
    fprintf(stderr, "capabilities.sampling.maxTokens: %d\n", capabilities->sampling.maxTokens);
    fprintf(stderr, "clientInfo.name: %s\n", clientInfo->name);
    fprintf(stderr, "clientInfo.version: %s\n", clientInfo->version);

    // 添加serverInfo
    cJSON* serverInfo = cJSON_CreateObject();
    if (!serverInfo) {
        cJSON_Delete(result);
        return NULL;
    }
    cJSON_AddItemToObject(result, "serverInfo", serverInfo);
    cJSON_AddStringToObject(serverInfo, "name", SERVER_NAME);
    cJSON_AddStringToObject(serverInfo, "version", SERVER_VERSION);
    int i=0;
    return result;
}


// --- Handler for the "notifications/initialized" method ---

/**
 * @brief Handles the 'notifications/initialized' JSON-RPC notification.
 * Exported as the handler for the "notifications/initialized" method via EXPORT_AS.
 *
 * @param params The cJSON object containing parameters (expected to be null or empty).
 */
EXPORT_AS(notifications, initialized)
cJSON* initialized_notification() {
    // 这里可以添加初始化完成后的处理逻辑
    printf("Server initialized successfully\n");
    cJSON* result = cJSON_CreateObject();
    return result;
}

EXPORT_AS(tools, list)
cJSON* handle_tools_list() {
    cJSON* result = get_all_function_signatures_json();
    return result;
}



