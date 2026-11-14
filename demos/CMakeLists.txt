cmake_minimum_required(VERSION 3.16)
project(ascend_cpp_demos LANGUAGES CXX)

if(DEFINED ENV{CANN_HOME})
    set(CANN_DEFAULT_PATH "$ENV{CANN_HOME}")
else()
    set(CANN_DEFAULT_PATH "/usr/local/Ascend/ascend-toolkit/latest")
endif()
set(ASCEND_CANN_PACKAGE_PATH "${CANN_DEFAULT_PATH}" CACHE STRING "CANN installation directory")
set(SOC_VERSION "Ascend310P3" CACHE STRING "Target Ascend SoC")
set(RUN_MODE "npu")
set(ASCEND_KERNEL_LAUNCH_ONLY ON)

set(ASCEND_ARCH_DIR "${ASCEND_CANN_PACKAGE_PATH}/${CMAKE_SYSTEM_PROCESSOR}-linux")
set(ASCENDC_CMAKE_FILE "")
foreach(candidate
        "${ASCEND_ARCH_DIR}/tikcpp/ascendc_kernel_cmake/ascendc.cmake"
        "${ASCEND_CANN_PACKAGE_PATH}/compiler/tikcpp/ascendc_kernel_cmake/ascendc.cmake"
        "${ASCEND_CANN_PACKAGE_PATH}/tools/tikcpp/ascendc_kernel_cmake/ascendc.cmake")
    if(EXISTS "${candidate}")
        set(ASCENDC_CMAKE_FILE "${candidate}")
        break()
    endif()
endforeach()
if(NOT ASCENDC_CMAKE_FILE)
    message(FATAL_ERROR "Cannot find ascendc.cmake under ${ASCEND_CANN_PACKAGE_PATH}")
endif()
include("${ASCENDC_CMAKE_FILE}")

add_executable(acl_memory_demo acl_memory_demo.cpp)
target_compile_features(acl_memory_demo PRIVATE cxx_std_17)
target_link_libraries(acl_memory_demo PRIVATE ascendcl)

add_executable(acl_gemm_demo acl_gemm_demo.cpp)
target_compile_features(acl_gemm_demo PRIVATE cxx_std_17)
target_link_libraries(acl_gemm_demo PRIVATE ascendcl opapi nnopbase)

ascendc_library(ascendc_add_kernel STATIC ascendc_add_kernel.cpp)

add_executable(ascendc_add_demo ascendc_add_demo.cpp)
target_compile_features(ascendc_add_demo PRIVATE cxx_std_17)
target_link_libraries(ascendc_add_demo PRIVATE ascendc_add_kernel ascendcl)

foreach(host_target acl_memory_demo acl_gemm_demo ascendc_add_demo)
    target_include_directories(${host_target} PRIVATE "${ASCEND_ARCH_DIR}/include")
    target_link_directories(${host_target} PRIVATE "${ASCEND_ARCH_DIR}/lib64")
endforeach()
