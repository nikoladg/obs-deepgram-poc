# deepgram/CMakeLists.txt

project(deepgram)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# to use asio + websocketpp without boost, one would specify the following:
#add_definitions(-DASIO_STANDALONE)
# however, this would not allow for tls/ssl support, which is a no-go for making a secure ws connection

# when installing openssl via homebrew, it will by default live here: /usr/local/opt/openssl
# theoretically, one can install boost via homebrew too and use /usr/local/opt/boost, but strange issues arise

# including BOOST the "good" way is tough because of openssl issues
# the two solutions presented in the following are:
# 1) including boost source yourself (https://github.com/Meenapintu/Restweb/issues/7)
# 2) manually altering the lcrypto shared library on OS X (https://github.com/ValveSoftware/source-sdk-2013/issues/413)
# I am doing (1) here
#SET(BOOST_ROOT /usr/local/opt/boost)
#SET(BOOST_INCLUDEDIR /usr/local/opt/boost/include)
#find_package(Boost REQUIRED)

#"${BOOST_INCLUDEDIR}"

include_directories(
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/websocketpp"
      "/usr/local/opt/openssl/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/asio/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/system/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/config/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/throw_exception/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/assert/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/align/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/date_time/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/smart_ptr/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/core/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/utility/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/static_assert/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/type_traits/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/numeric/conversion/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/mpl/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/preprocessor/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/bind/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/regex/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/boost/boost/libs/predef/include"
      )

      #       "${CMAKE_CURRENT_SOURCE_DIR}/deps/asio/asio/include"

set(deepgram_SOURCES
      deepgram.cpp
      deepgram_source.cpp)

add_library(deepgram MODULE
      ${deepgram_SOURCES})
target_link_libraries(deepgram
      libobs
      /usr/local/opt/openssl/lib
      /usr/local/opt/openssl/lib/libcrypto.a
      /usr/local/opt/openssl/lib/libssl.a
      )

install_obs_plugin_with_data(deepgram data)