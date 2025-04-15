cmake_minimum_required(VERSION 3.15)

project(MCPC)

# 添加自定义模块路径
set(CMAKE_CXX_STANDARD 17)
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")
# # 设置生成编译数据库
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# # 创建编译命令数据库的路径
# set(COMPILE_COMMANDS_JSON "${CMAKE_BINARY_DIR}/compile_commands.json")
message(STATUS "compile_commands.json: ${COMPILE_COMMANDS_JSON}")

# Add subdirectory for the 'export' executable
add_subdirectory(export)

#3. generate code file for dependency
file(GLOB_RECURSE SOURCE_NEED_TO_BE_GENERATED
    ${PROJECT_SOURCE_DIR}/src/**/*.cpp
    ${PROJECT_SOURCE_DIR}/src/**/*.c
)
# 排除不需要的文件
file(GLOB_RECURSE EXCLUDED_FILES
    ${PROJECT_SOURCE_DIR}/src/base/mcp.c
    ${PROJECT_SOURCE_DIR}/src/generated_src/*
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
    file(RELATIVE_PATH rel_path ${PROJECT_SOURCE_DIR}/src ${source_file}) # Modified relative path
    # 获取文件名（不带扩展名）
    get_filename_component(file_name ${rel_path} NAME_WE)
    # 构建生成文件的路径
    set(generated_file ${PROJECT_SOURCE_DIR}/src/generated_src/${file_name}_bridge.c)
    list(APPEND GENERATED_SOURCES ${generated_file})
    # Create the directory if it doesn't exist
    file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/src/generated_src)
    # 创建生成文件
    add_custom_command(
        OUTPUT ${generated_file}
        COMMAND ${CMAKE_COMMAND} -E touch ${generated_file}
        DEPENDS ${SOURCE_NEED_TO_BE_GENERATED}
        VERBATIM # Important for proper dependency tracking
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
target_include_directories(mcpc PRIVATE ${PROJECT_SOURCE_DIR}/src) # Add the main src directory

# #curl
# find_package(curl REQUIRED)
# if(curl_FOUND)
#     message(STATUS "Found CURL library: ${CURL_LIBRARIES}")
#     message(STATUS "Found CURL include directory: ${CURL_INCLUDE_DIRS}")
#     # Link your executable or library against curl
#     target_link_libraries(mcpc PRIVATE CURL::libcurl)
#     # Include the curl headers in your source files
#     target_include_directories(mcpc PRIVATE ${CURL_INCLUDE_DIRS})
#     list(APPEND MCPC_INCLUDE_DIRS ${CURL_INCLUDE_DIRS})
# else()
#     message(FATAL_ERROR "CURL library not found. Make sure it's installed via vcpkg and the toolchain file is correctly set.")
# endif()
#cJSON
find_package(cJSON REQUIRED)
if(cJSON_FOUND)
    message(STATUS "Found CJSON library: ${CJSON_LIBRARIES}")
    message(STATUS "Found CJSON include directory: ${CJSON_INCLUDE_DIRS}")
    # Link your executable or library against curl
    target_link_libraries(mcpc PRIVATE ${CJSON_LIBRARIES})
    # Include the curl headers in your source files
    target_include_directories(mcpc PRIVATE ${CJSON_INCLUDE_DIRS}/cjson)
    list(APPEND MCPC_INCLUDE_DIRS ${CJSON_INCLUDE_DIRS}/cjson)
else()
    message(FATAL_ERROR "CJSON library not found. Make sure it's installed via vcpkg and the toolchain file is correctly set.")
endif()

set(EXPORT_INCLUDE_ARGS "")
foreach(INCLUDE_DIR IN LISTS MCPC_INCLUDE_DIRS)
    # Append each directory prefixed with -I to the arguments list
    list(APPEND EXPORT_INCLUDE_ARGS "-I${INCLUDE_DIR}")
endforeach()
#generated code
set(FUNCTION_SIGNATURES_OUTPUT "${PROJECT_SOURCE_DIR}/src/generated_src/generated_function_signatures.c")
set(BRIDGE_CODE_OUTPUT "${PROJECT_SOURCE_DIR}/src/generated_src/generated_bridge_code.c")
add_custom_command(
    OUTPUT ${FUNCTION_SIGNATURES_OUTPUT} ${BRIDGE_CODE_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/src/generated_src"
    COMMAND $<TARGET_FILE:export>
            ${SOURCE_NEED_TO_BE_GENERATED}
            -s ${FUNCTION_SIGNATURES_OUTPUT}
            -b ${BRIDGE_CODE_OUTPUT}
            -o ${PROJECT_SOURCE_DIR}/src/generated_src
            --
            ${EXPORT_INCLUDE_ARGS}
    DEPENDS ${SOURCE_NEED_TO_BE_GENERATED} export ${COMPILE_COMMANDS_JSON} # Changed dependency to generated list
    VERBATIM
)
add_custom_target(generate_code
    DEPENDS ${FUNCTION_SIGNATURES_OUTPUT} ${BRIDGE_CODE_OUTPUT}
)
add_dependencies(mcpc generate_code)



# 打印调试信息
message(STATUS "MCP Server sources:")
foreach(source_file
        ${MCPC_SOURCES}  # 添加所有收集到的mcp_server源文件
        ${GENERATED_SOURCES}
)
    message(STATUS "  ${source_file}")
endforeach()