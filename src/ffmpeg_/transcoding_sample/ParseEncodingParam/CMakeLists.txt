CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(ParseEncodingParam)

AUX_SOURCE_DIRECTORY(. DIR_SRC)

ADD_DEFINITIONS("-g -c -fPIC -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0
                 -z noexecstack -z relro -z now -fstack-protector-strong
                 -fPIE -fPIC -pie -O2 -D_FORTIFY_SOURCE=2 -Wformat
                 -Wformat-security -Wl,-S -Wall -Werror")


INCLUDE_DIRECTORIES(/usr/local/include)
LINK_DIRECTORIES(/usr/local/lib)

SET(DIR_SRC
    ${DIR_SRC}
)

ADD_LIBRARY(ParseEncodingParam SHARED ${DIR_SRC})

TARGET_LINK_LIBRARIES(ParseEncodingParam dl)

INSTALL(TARGETS ParseEncodingParam
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static)

INSTALL(FILES ${PROJECT_SOURCE_DIR}/EncodingParamsDef.h DESTINATION include)
INSTALL(FILES ${PROJECT_SOURCE_DIR}/ParseEncodingParamAPI.h DESTINATION include)
INSTALL(FILES ${PROJECT_SOURCE_DIR}/ParseEncodingParam.pc DESTINATION lib/pkgconfig)
