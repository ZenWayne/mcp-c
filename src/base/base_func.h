#ifndef BASE_FUNC_H
#define BASE_FUNC_H
#include <stdbool.h>

#include "export_macro.h"
// --- Server Information (Constants) ---
#define SERVER_NAME "secure-filesystem-server" // Server name
#define SERVER_VERSION "0.2.0"                 // Server version
#define PROTOCOL_VERSION "2024-11-05"          // Protocol version from example

typedef struct EXPORT client_info {
    char* name;
    char* version;
} client_info;

typedef struct EXPORT sampling {
    int maxTokens;
} sampling;

typedef struct EXPORT root {
    bool listChanged;
} root;

typedef struct EXPORT capabilities {
    struct root roots;
    struct sampling sampling;
} capabilities;

typedef struct EXPORT initialize_params {
    char* protocolVersion;
    struct capabilities capabilities;
    struct client_info clientInfo;
} initialize_params;

cJSON* initialize(char* protocolVersion, struct capabilities* capabilities, struct client_info* clientInfo);
cJSON* initialized_notification();
cJSON* handle_tools_list();

#endif
