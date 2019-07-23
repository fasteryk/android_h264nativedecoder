cmake_minimum_required(VERSION 3.4.1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -UNDEBUG -O3")

add_library(native-codec SHARED
            looper.cpp
            native_codec.cpp)

add_library(lib_ncnn STATIC IMPORTED)
set_target_properties(lib_ncnn
                        PROPERTIES
                        IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/ncnn/lib/libncnn.a)

include_directories("${CMAKE_SOURCE_DIR}/ncnn/include")

find_package( OpenMP REQUIRED)
if(OPENMP_FOUND)
    message("OPENMP FOUND")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
#    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

set(ANDROID_OPENCV_COMPONENTS "opencv_java" CACHE STRING "")
message(STATUS "ANDROID_ABI=${ANDROID_ABI}")
find_package(OpenCV REQUIRED COMPONENTS ${ANDROID_OPENCV_COMPONENTS})

include_directories("${CMAKE_CURRENT_LIST_DIR}")
include_directories(${OpenCV_INCLUDE_DIRS})

# Include libraries needed for native-codec-jni lib
target_link_libraries(native-codec
                      android
                      log
                      mediandk
                      OpenMAXAL
                      lib_ncnn
                      ${ANDROID_OPENCV_COMPONENTS}
                      ${OpenMP_EXE_LINKER_FLAGS})
