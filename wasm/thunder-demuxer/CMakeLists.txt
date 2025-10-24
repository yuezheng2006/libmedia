cmake_minimum_required(VERSION 3.10)
project(ThunderPlayerWASM)

# 设置Emscripten编译选项
set(CMAKE_EXECUTABLE_SUFFIX ".js")

# 设置WASM输出目录路径（相对于当前CMakeLists.txt文件）
set(WASM_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../public/wasm")

# 输出路径信息
message(STATUS "WASM output directory: ${WASM_OUTPUT_DIR}")

# 添加头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg/libavcodec)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg/libavformat)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg/libavutil)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg/libswscale)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/openssl)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/tdcrypto)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/decoder)

message(STATUS "Using OpenSSL headers from: ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "Using FFmpeg headers from: ${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg")
message(STATUS "Using TDCrypto headers from: ${CMAKE_CURRENT_SOURCE_DIR}/include/tdcrypto")

# 设置总内存 (Windows优先优化：增大内存减少动态分配)
# 128MB = 134217728, 256MB = 268435456, 320MB = 335544320
set(TOTAL_MEMORY 335544320)

# 设置Emscripten编译和链接标志
set(EM_FLAGS "
    -s WASM=1
    -s EXPORTED_RUNTIME_METHODS=['HEAPU8','HEAP32','UTF8ToString','stringToUTF8','ccall','cwrap','addFunction']
    -s EXPORTED_FUNCTIONS=['_malloc','_free','_main','_js_init_auth','_js_get_version','_http_response_handler','_PEM_read_bio_RSA_PUBKEY','_initDecoder','_uninitDecoder','_openDecoder','_closeDecoder','_sendData','_decodeOnePacket','_switchAudioTrack','_seekTo','_printFifoRate','_getFifoUsedSize','_getFifoTotalSize','_get_auth_status_wrapper','_js_readOnePacket','_readOnePacket','_js_setPacketCallback','_setPacketCallback']
    -s ALLOW_MEMORY_GROWTH=1
    -s ASSERTIONS=1
    -s ERROR_ON_UNDEFINED_SYMBOLS=1
    -s EXIT_RUNTIME=0
    -s ASYNCIFY=1
    -s FORCE_FILESYSTEM=1
    -s LINKABLE=1
    -s EXPORT_ALL=1
    -s RESERVED_FUNCTION_POINTERS=14
    -s TOTAL_MEMORY=${TOTAL_MEMORY}
    -s INITIAL_MEMORY=${TOTAL_MEMORY}
    -s MAXIMUM_MEMORY=536870912
    -s ALLOW_MEMORY_GROWTH=1
    -s MALLOC=emmalloc
    -s ENVIRONMENT=web
    -O3
")

string(REPLACE "\n" " " EM_FLAGS_FLAT ${EM_FLAGS})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EM_FLAGS_FLAT}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EM_FLAGS_FLAT}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EM_FLAGS_FLAT}")

# 添加所有源文件到thunder_module
add_executable(thunder_module 
    thunder_module.c 
    http_client.c
    safe_logger.c
    decoder/decoder.c
    decoder/log.c
    decoder/tdcrypto/tsDecrypt.c
)

# 添加OpenSSL/FFmpeg/TDCrypto库
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib_native")
message(STATUS "Using libraries from: ${LIB_DIR}")

# 链接所有库
target_link_libraries(thunder_module 
    ${LIB_DIR}/libcrypto.a
    ${LIB_DIR}/libssl.a
    ${LIB_DIR}/libavformat.a
    ${LIB_DIR}/libavcodec.a
    ${LIB_DIR}/libavutil.a
    ${LIB_DIR}/libswscale.a
    ${LIB_DIR}/libtdcrypto.a
)

# 设置thunder_module输出目录
set_target_properties(thunder_module PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WASM_OUTPUT_DIR}"
)

# 对于Release模式设置额外的优化标志
set_target_properties(thunder_module PROPERTIES
    COMPILE_FLAGS_RELEASE "-Os -flto -s ASSERTIONS=0 -s ENVIRONMENT='web' --closure 1 -s SINGLE_FILE=0"
)

# 安装规则
install(TARGETS thunder_module DESTINATION ${WASM_OUTPUT_DIR}) 