cmake_minimum_required(VERSION 3.16)
project(ScanReceiver CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ScanReceiver WIN32 main.cpp ftp_server.cpp ftp_server.h app.rc)
target_compile_definitions(ScanReceiver PRIVATE UNICODE _UNICODE)
target_link_libraries(ScanReceiver PRIVATE ws2_32 comctl32 shell32 ole32 uuid dwmapi winmm advapi32 gdiplus msimg32)

if(MSVC)
  target_compile_options(ScanReceiver PRIVATE /utf-8 /W4)
  target_link_options(ScanReceiver PRIVATE /MANIFEST:NO)
else()
  target_link_options(ScanReceiver PRIVATE -municode -static)
endif()
