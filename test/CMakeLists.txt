include(CTest)
include(../cmake/FindBitcoinApiCpp.cmake)

find_package(CURL)

# Turn off some warnings to silence issues coming from googletest code
if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
elseif(CMAKE_CXX_COMPILER_ID MATCHES Clang)
  set(DCD_CXX_FLAGS ${DCD_CXX_FLAGS} -Wno-global-constructors)
endif()

option(INSTALL_GTEST "Enable installation of googletest." OFF)
add_subdirectory("googletest")

# Turn off some warnings to silence issues coming from rapidcheck code
if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
elseif(CMAKE_CXX_COMPILER_ID MATCHES Clang)
  set(DCD_CXX_FLAGS ${DCD_CXX_FLAGS} -Wno-c++98-compat-pedantic)
endif()

option(RC_ENABLE_GTEST "Build Google Test integration" ON)
add_subdirectory("rapidcheck")



############################################################
# Target: UnitTests_src

add_executable(UnitTests_src main.cpp test_chainSoQuery.cpp test_classifyInputString.cpp test_encodeOpReturnData.cpp test_satoshis.cpp jsonTestData.h)

target_compile_features(UnitTests_src PRIVATE cxx_std_11)
target_compile_options(UnitTests_src PRIVATE ${DCD_CXX_FLAGS})
set_target_properties(UnitTests_src PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(UnitTests_src
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/libbech32
        ${PROJECT_SOURCE_DIR}/libtxref
	${JSONCPP_INCLUDE_DIRS}
	${BITCOINAPICPP_INCLUDE_DIRS})

target_link_libraries(UnitTests_src
    PUBLIC
        txref bech32 nlohmann-json ${JSONCPP_LIBRARIES} ${BITCOINAPICPP_LIBRARIES} ${CURL_LIBRARIES} gtest gmock rapidcheck_gtest)

# CTest targets

add_test(NAME UnitTests_src
        COMMAND UnitTests_src)

add_subdirectory(testdomain)
