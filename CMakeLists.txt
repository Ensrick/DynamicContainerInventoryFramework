cmake_minimum_required(VERSION 3.24)

option(BUILD_TEST "Sets log level to debug." OFF)

# -------- Project ----------
project(
	ContainerDistributionFramework
	VERSION 3.1.0
	LANGUAGES CXX
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_IGNORE_PATH "${PROJECT_BINARY_DIR}")
list(APPEND CMAKE_MODULE_PATH
	"${PROJECT_SOURCE_DIR}/tools/SKSE-CMakeModules/Modules"
)
include(SKSEPlugin)
set(SKSE_USE_XBYAK ON)
find_path(CLIB_UTIL_INCLUDE_DIRS "ClibUtil/utils.hpp")

add_compile_definitions(SKYRIM_AE)
add_compile_definitions(SKYRIM_SUPPORT_AE)
add_compile_definitions(SKSE_SUPPORT_XBYAK)

if (BUILD_TEST)
	add_compile_definitions(DEBUG)
endif()

SKSEPlugin_Add(
	${PROJECT_NAME}
	SOURCE_DIR src
	SOURCES
		README.md
		vcpkg.json
	PRECOMPILE_HEADERS
		src/common/PCH.h
)


find_package(jsoncpp CONFIG REQUIRED)

target_link_libraries(
		"${PROJECT_NAME}"
		PRIVATE
			JsonCpp::JsonCpp
)

set_target_properties(CommonLibSSE PROPERTIES
	FOLDER External
)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CLIB_UTIL_INCLUDE_DIRS}
)

include(CTest)
if(BUILD_TESTING)
	add_executable(count-safety-tests tests/count-safety-tests.cpp)
	target_compile_features(count-safety-tests PRIVATE cxx_std_20)
	target_include_directories(count-safety-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
	add_test(NAME count-safety COMMAND count-safety-tests)

	if(MSVC)
		target_compile_options(count-safety-tests PRIVATE /W4 /WX)
	endif()
endif()

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
endif()

set(DATA_DIR "${PROJECT_SOURCE_DIR}/data")

install(
	FILES "${DATA_DIR}/SKSE/Plugins/replaceme.ini"
	DESTINATION "SKSE/Plugins"
)

install(
	FILES "${PROJECT_SOURCE_DIR}/LICENSE"
	DESTINATION "."
	RENAME "LICENSE-Apache-2.0.txt"
	COMPONENT "SKSEPlugin"
)

install(
	FILES
		"${PROJECT_SOURCE_DIR}/SOURCE-NOTICE.txt"
		"${PROJECT_SOURCE_DIR}/BUILD-MANIFEST.json"
	DESTINATION "."
	COMPONENT "SKSEPlugin"
)

include(components.cmake)

# ---- Post build ----
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
	if(BUILD_TEST)
		set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME} - Test")
	else() 
		set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
	endif()
endif()

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
