/**
 * @file    expression_parser.h
 * @brief   Parses the text of an expression into the compact form that is evaluated.
 */

#ifndef NODALYS_EXPRESSION_PARSER_H
#define NODALYS_EXPRESSION_PARSER_H

#include "expression/expression_program.h"
#include <expected>
#include <string>
#include <string_view>

namespace Nodalys {

// Parsing
std::expected<ExpressionProgram, std::string> ParseExpressionProgram(std::string_view text);

} // namespace Nodalys

#endif // NODALYS_EXPRESSION_PARSER_H
