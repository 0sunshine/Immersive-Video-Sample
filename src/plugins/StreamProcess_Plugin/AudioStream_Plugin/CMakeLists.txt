CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

PROJECT(AACAudioStreamProcess)

AUX_SOURCE_DIRECTORY(. DIR_SRC)

ADD_DEFINITIONS("-g -c -fPIC -lglog -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -z noexecstack -z relro -z now -fstack-protector-strong -fPIE -fPIC -pie -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Wl,-S -Wall -Werror")

INCLUDE_DIRECTORIES(/usr/local/include ../../../utils ./)
LINK_DIRECTORIES(/usr/local/lib)

ADD_LIBRARY(AACAudioStreamProcess SHARED ${DIR_SRC})

TARGET_LINK_LIBRARIES(AACAudioStreamProcess glog)

install(TARGETS AACAudioStreamProcess
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static)
