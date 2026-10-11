enable_language(CXX)

add_executable(evaluator main.cpp)
add_compile_definitions(ORDERED_OUTPUT=1)

find_program(HAS_CLANG_TIDY clang-tidy)
if(NOT HAS_CLANG_TIDY)
    set_target_properties(err demo_policy demo_env PROPERTIES C_CLANG_TIDY "")
endif()

