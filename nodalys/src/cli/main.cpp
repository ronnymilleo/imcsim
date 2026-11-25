/**
 * @file    main.cpp
 * @brief   Entry point of the standalone Nodalys command-line tool.
 */

#include "cli/cli.h"

#include <iostream>

/**
 * @brief   Entry point for the standalone `nodalys` CLI executable.
 * @param[in] argc  Argument count.
 * @param[in] argv  Argument vector.
 * @return  Process exit status (`0` on success).
 */
int main(const int argc, char **argv) {
    return Nodalys::RunCli(argc, argv, std::cin, std::cout, std::cerr);
}
