// 单测入口（doctest）。
//
// 这个二进制只链纯 C++ target，所以它不初始化 Qt、不加载 Qt DLL。
// 一旦哪天这里必须建 QObject，说明领域层被污染了——那才是要报警的信号。
//
// 运行：xmake build sonora_tests && xmake run sonora_tests
// 单跑某个用例：xmake run sonora_tests -tc="Result*"
#include <ostream>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>
