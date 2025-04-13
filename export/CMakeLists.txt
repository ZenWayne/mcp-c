cmake_minimum_required(VERSION 3.15)

project(export_project) # Give it a different project name to avoid confusion

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