cmake_minimum_required(VERSION 3.15)

project(MCPC)

# 添加自定义模块路径
set(CMAKE_CXX_STANDARD 17)
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
# 设置生成编译数据库
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 创建编译命令数据库的路径
set(COMPILE_COMMANDS_JSON "${CMAKE_BINARY_DIR}/compile_commands.json")
message(STATUS "compile_commands.json: ${COMPILE_COMMANDS_JSON}")

#1. export 
add_executable(export ${PROJECT_SOURCE_DIR}/src/export.cpp)
find_package(LLVM CONFIG REQUIRED)
if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    # 设置 LLVM 定义和包含目录
    add_definitions(${LLVM_DEFINITIONS})
    include_directories(${LLVM_INCLUDE_DIRS})
    # 设置 LLVM 组件
    llvm_map_components_to_libnames(llvm_libs Support)
    target_include_directories(export PRIVATE ${LLVM_INCLUDE_DIRS})
    message(STATUS "llvm_libs: ${llvm_libs}")
    message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
    target_link_libraries(export PRIVATE ${llvm_libs})
endif()
find_package(Clang REQUIRED Basic AST Frontend Tooling ASTMatchers)
if(Clang_FOUND)
    message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")
    target_include_directories(export PRIVATE ${CLANG_INCLUDE_DIRS})
    message(STATUS "Clang_INCLUDE_DIRS: ${CLANG_INCLUDE_DIRS}")
    target_link_libraries(export PRIVATE 
        clangBasic
        clangAST
        clangFrontend
        clangTooling
        clangASTMatchers
    )
endif()


#3. generate code file for dependency
file(GLOB_RECURSE SOURCE_NEED_TO_BE_GENERATED
    ${PROJECT_SOURCE_DIR}/src/**/*.cpp
    ${PROJECT_SOURCE_DIR}/src/**/*.c
)
# 排除不需要的文件
file(GLOB_RECURSE EXCLUDED_FILES 
    ${PROJECT_SOURCE_DIR}/src/base/mcp.c
    # 移除对 base 目录的排除
)
list(REMOVE_ITEM SOURCE_NEED_TO_BE_GENERATED ${EXCLUDED_FILES})
message(STATUS "SOURCE_NEED_TO_BE_GENERATED: ${SOURCE_NEED_TO_BE_GENERATED}")
# 创建生成文件并收集生成文件路径
set(GENERATED_SOURCES 
    ${PROJECT_SOURCE_DIR}/src/generated_src/generated_function_signatures.c
    ${PROJECT_SOURCE_DIR}/src/generated_src/generated_bridge_code.c
)
foreach(source_file ${SOURCE_NEED_TO_BE_GENERATED})
    # 获取相对路径
    file(RELATIVE_PATH rel_path ${PROJECT_SOURCE_DIR}/src/mcp_server ${source_file})
    # 获取文件名（不带扩展名）
    get_filename_component(file_name ${rel_path} NAME_WE)
    # 构建生成文件的路径
    set(generated_file ${PROJECT_SOURCE_DIR}/src/generated_src/${file_name}_bridge.c)
    list(APPEND GENERATED_SOURCES ${generated_file})
    # 创建生成文件
    add_custom_command(
        OUTPUT ${generated_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/src/generated_src
        COMMAND ${CMAKE_COMMAND} -E touch ${generated_file}
        DEPENDS ${source_file}
    )
endforeach()
message(STATUS "GENERATED_SOURCES: ${GENERATED_SOURCES}")
# 收集所有需要编译的源文件
file(GLOB_RECURSE MCPC_SOURCES 
    ${PROJECT_SOURCE_DIR}/src/**/*.cpp
    ${PROJECT_SOURCE_DIR}/src/**/*.c
    ${PROJECT_SOURCE_DIR}/src/main.c
)
# 添加主可执行文件
add_executable(mcpc
    ${MCPC_SOURCES}
    ${GENERATED_SOURCES}
)

# 递归查找所有子目录并添加到包含路径 do not forget to add "/" at the end of the path
file(GLOB_RECURSE MCPC_INCLUDE_DIRS LIST_DIRECTORIES true "${PROJECT_SOURCE_DIR}/src/base") #why /src and /src/ not work ????
message(STATUS "MCPC_INCLUDE_DIRS: ${MCPC_INCLUDE_DIRS}")
foreach(dir ${MCPC_INCLUDE_DIRS})
    target_include_directories(mcpc PRIVATE ${dir})
endforeach()


#generated code
set(FUNCTION_SIGNATURES_OUTPUT "${PROJECT_SOURCE_DIR}/src/generated_src/generated_function_signatures.c")
set(BRIDGE_CODE_OUTPUT "${PROJECT_SOURCE_DIR}/src/generated_src/generated_bridge_code.c")
add_custom_command(
    OUTPUT ${FUNCTION_SIGNATURES_OUTPUT} ${BRIDGE_CODE_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}"
    COMMAND $<TARGET_FILE:export> 
            -p "${CMAKE_BINARY_DIR}" 
            ${SOURCE_NEED_TO_BE_GENERATED} 
            -s ${FUNCTION_SIGNATURES_OUTPUT} 
            -b ${BRIDGE_CODE_OUTPUT}
            -o ${PROJECT_SOURCE_DIR}/src/generated_src
            -- 
            ${CMAKE_COMMAND} -E echo "-I$<JOIN:${MCPC_INCLUDE_DIRS}, -I>"
    DEPENDS ${MCP_SERVER_SOURCES} export ${COMPILE_COMMANDS_JSON}
    VERBATIM
)
add_custom_target(generate_code
    DEPENDS ${FUNCTION_SIGNATURES_OUTPUT} ${BRIDGE_CODE_OUTPUT}
)
add_dependencies(mcpc generate_code)

#curl
find_package(curl REQUIRED)
if(curl_FOUND)
    message(STATUS "Found CURL library: ${CURL_LIBRARIES}")
    message(STATUS "Found CURL include directory: ${CURL_INCLUDE_DIRS}")
    # Link your executable or library against curl
    target_link_libraries(mcpc PRIVATE CURL::libcurl)
    # Include the curl headers in your source files
    target_include_directories(mcpc PRIVATE ${CURL_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "CURL library not found. Make sure it's installed via vcpkg and the toolchain file is correctly set.")
endif()
#cJSON
find_package(cJSON REQUIRED)
if(cJSON_FOUND)
    message(STATUS "Found CJSON library: ${CJSON_LIBRARIES}")
    message(STATUS "Found CJSON include directory: ${CJSON_INCLUDE_DIRS}")
    # Link your executable or library against curl
    target_link_libraries(mcpc PRIVATE ${CJSON_LIBRARIES})
    # Include the curl headers in your source files
    target_include_directories(mcpc PRIVATE ${CJSON_INCLUDE_DIRS}/cjson)
else()
    message(FATAL_ERROR "CJSON library not found. Make sure it's installed via vcpkg and the toolchain file is correctly set.")
endif()

# 打印调试信息
message(STATUS "MCP Server sources:")
foreach(source_file 
        ${MCPC_SOURCES}  # 添加所有收集到的mcp_server源文件
        ${GENERATED_SOURCES}
)
    message(STATUS "  ${source_file}")
endforeach()