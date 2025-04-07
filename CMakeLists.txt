cmake_minimum_required(VERSION 3.15)
set(LLVM_ENABLE_DIA_SDK OFF)
project(MCPC)


# 添加自定义模块路径
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

find_package(Clang REQUIRED Tooling ASTMatchers)
find_package(CURL REQUIRED)
include_directories(${CLANG_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 17)
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 添加项目的include目录
include_directories(${PROJECT_SOURCE_DIR}/src/include)


include_directories(${PROJECT_SOURCE_DIR}/src/mcp_server)

# 为MSVC设置MD运行库以匹配LLVM (确保这段代码在 add_subdirectory(cJSON) 之前)
if(MSVC)
  # 强制使用Release模式的运行库
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC Runtime Library" FORCE)
  # 确保 C 和 C++ 编译器标志包含 /MD 和正确的调试级别宏
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MD /D_ITERATOR_DEBUG_LEVEL=0" CACHE STRING "C Compiler Flags" FORCE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MD /D_ITERATOR_DEBUG_LEVEL=0" CACHE STRING "C++ Compiler Flags" FORCE)
endif()

# 添加cJSON库 (这行应该在 if(MSVC) 之后)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/cJSON)

# 设置生成编译数据库
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 定义输出文件的路径
set(FUNCTION_SIGNATURES_OUTPUT "${PROJECT_SOURCE_DIR}/src/generated_function_signatures.c")
set(BRIDGE_CODE_OUTPUT "${PROJECT_SOURCE_DIR}/src/generated_bridge_code.c")

# 收集所有需要编译的源文件
file(GLOB_RECURSE MCP_SERVER_SOURCES 
    ${PROJECT_SOURCE_DIR}/src/mcp_server/**/*.cpp
    ${PROJECT_SOURCE_DIR}/src/mcp_server/**/*.c
)

# 排除不需要的文件
file(GLOB_RECURSE EXCLUDED_FILES 
    ${PROJECT_SOURCE_DIR}/src/mcp_server/base/mcp.c
    # 移除对 base 目录的排除
)
list(REMOVE_ITEM MCP_SERVER_SOURCES ${EXCLUDED_FILES})

# 创建编译命令数据库的路径
set(COMPILE_COMMANDS_JSON "${CMAKE_BINARY_DIR}/compile_commands.json")

message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")

add_executable(extract_signatures ${PROJECT_SOURCE_DIR}/src/export.cpp)
target_link_libraries(extract_signatures PRIVATE clangTooling clangASTMatchers clangFrontend clangDriver clangBasic)

add_custom_command(
    OUTPUT ${FUNCTION_SIGNATURES_OUTPUT} ${BRIDGE_CODE_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}"
    COMMAND $<TARGET_FILE:extract_signatures> 
            -p "${CMAKE_BINARY_DIR}" 
            ${MCP_SERVER_SOURCES} 
            -s ${FUNCTION_SIGNATURES_OUTPUT} 
            -b ${BRIDGE_CODE_OUTPUT}
    DEPENDS ${MCP_SERVER_SOURCES} extract_signatures ${COMPILE_COMMANDS_JSON}
    VERBATIM
)

add_custom_target(generate_code
    DEPENDS ${FUNCTION_SIGNATURES_OUTPUT} ${BRIDGE_CODE_OUTPUT}
)

# 递归查找builtin目录下所有的cpp和h文件
file(GLOB_RECURSE BUILTIN_SOURCES 
    ${PROJECT_SOURCE_DIR}/src/builtin/**/*.cpp
    ${PROJECT_SOURCE_DIR}/src/builtin/**/*.h
)

# 添加主可执行文件，这次只包含 main.c
add_executable(mcpc
    ${PROJECT_SOURCE_DIR}/src/main.c
    ${PROJECT_SOURCE_DIR}/src/mcp_server/base/mcp.c
    ${FUNCTION_SIGNATURES_OUTPUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/cJSON.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cJSON/cJSON_Utils.h
    ${BRIDGE_CODE_OUTPUT}
    ${BUILTIN_SOURCES}
    ${MCP_SERVER_SOURCES}  # 添加所有收集到的mcp_server源文件
)

# 其他依赖仍然注释掉
target_link_libraries(mcpc PRIVATE CURL::libcurl)
# add_dependencies(mcpc generate_code)

# # 平台特定的设置
# if(MSVC)
#     add_definitions(-DBUILDING_DLL)
#     message(STATUS "MSVC: Building with DLL export definitions")
    
#     # 为所有源文件添加编译选项
#     foreach(source_file ${MCP_SERVER_SOURCES})
#         if(source_file MATCHES "\\.c$")
#             set_source_files_properties(${source_file} PROPERTIES COMPILE_DEFINITIONS "BUILDING_DLL")
#         endif()
#     endforeach()
# endif()

# 打印调试信息
message(STATUS "MCP Server sources:")
foreach(source_file 
        ${PROJECT_SOURCE_DIR}/src/main.c
        ${PROJECT_SOURCE_DIR}/src/mcp_server/base/mcp.c
        ${FUNCTION_SIGNATURES_OUTPUT}
        ${BRIDGE_CODE_OUTPUT}
        ${BUILTIN_SOURCES}
        ${MCP_SERVER_SOURCES}  # 添加所有收集到的mcp_server源文件
)
    message(STATUS "  ${source_file}")
endforeach()