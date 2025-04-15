cmake_minimum_required(VERSION 3.21)

project(export_project) # Give it a different project name to avoid confusion

set(VCPKG_APPLOCAL_DEPS ON)
add_executable(export ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

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

#copy zlib1.dll
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SUFFIX "debug/bin")
else()
    set(SUFFIX "bin")
endif()
add_custom_command(TARGET export POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different
                   "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/${SUFFIX}/zlib1.dll"
                   "$<TARGET_FILE_DIR:export>")

# 复制所有需要的DLL到可执行文件目录
add_custom_command(TARGET export POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<$<CONFIG:Debug>:$<TARGET_RUNTIME_DLLS:export>>"
        "$<$<CONFIG:Release>:$<TARGET_RUNTIME_DLLS:export>>"
        "$<TARGET_FILE_DIR:export>"
    COMMAND_EXPAND_LISTS
    DEPENDS $<TARGET_FILE:export>
    COMMENT "Copying runtime DLLs for export"
)