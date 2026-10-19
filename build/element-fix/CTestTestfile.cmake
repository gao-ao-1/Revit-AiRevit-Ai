# CMake generated Testfile for 
# Source directory: E:/Revit-AI/RecvtConsole
# Build directory: E:/Revit-AI/RecvtConsole/build/element-fix
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[recvt_protocol]=] "E:/Revit-AI/RecvtConsole/build/element-fix/recvt_protocol_test.exe")
set_tests_properties([=[recvt_protocol]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/Revit-AI/RecvtConsole/CMakeLists.txt;41;add_test;E:/Revit-AI/RecvtConsole/CMakeLists.txt;0;")
add_test([=[recvt_preview]=] "E:/Revit-AI/RecvtConsole/build/element-fix/recvt_preview_test.exe")
set_tests_properties([=[recvt_preview]=] PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/qt6/6.11.1/llvm-mingw_64/plugins" _BACKTRACE_TRIPLES "E:/Revit-AI/RecvtConsole/CMakeLists.txt;45;add_test;E:/Revit-AI/RecvtConsole/CMakeLists.txt;0;")
add_test([=[recvt_model_browser]=] "E:/Revit-AI/RecvtConsole/build/element-fix/recvt_model_browser_test.exe")
set_tests_properties([=[recvt_model_browser]=] PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=D:/qt6/6.11.1/llvm-mingw_64/plugins" _BACKTRACE_TRIPLES "E:/Revit-AI/RecvtConsole/CMakeLists.txt;51;add_test;E:/Revit-AI/RecvtConsole/CMakeLists.txt;0;")
add_test([=[recvt_error_guard]=] "E:/Revit-AI/RecvtConsole/build/element-fix/recvt_error_guard_test.exe")
set_tests_properties([=[recvt_error_guard]=] PROPERTIES  _BACKTRACE_TRIPLES "E:/Revit-AI/RecvtConsole/CMakeLists.txt;69;add_test;E:/Revit-AI/RecvtConsole/CMakeLists.txt;0;")
