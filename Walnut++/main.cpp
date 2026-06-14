#define WALNUT_DEBUG

#include "compiler.hpp"
#include <iostream>
#include <fstream>
#include <string>

int main() {
    try {
        const std::string& input_file = "input.wal";
        walnut::CompilerPipeline compiler(input_file);
        compiler.report_to_stderr();
        compiler.enable_ast_output();
        // compiler.report_timing();
        compiler.compile();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}