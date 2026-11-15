/**
 * @file    main.cpp
 * @brief   Entry point of the standalone Nodalys command-line tool.
 */

#include "cli/cli.h"

#include <iostream>

int main(const int argc, char **argv) {
    return Nodalys::RunCli(argc, argv, std::cin, std::cout, std::cerr);
}
