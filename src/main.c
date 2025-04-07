#include <stdio.h>
#include "base/mcp.h"
// #include "base\mcp.h" // 暂时不需要 mcp.h

#ifdef __cplusplus
extern "C" {
#endif

int main() {
    printf("mcp server is running...\n");
    mcp_serve();
    return 0;
}

#ifdef __cplusplus
}
#endif

