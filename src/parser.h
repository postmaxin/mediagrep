// Recursive-descent parser for mediagrep query expressions.
//
//   expr     := or_expr
//   or_expr  := and_expr ( "or" and_expr )*
//   and_expr := not_expr ( "and" not_expr )*
//   not_expr := "not" not_expr | atom
//   atom     := "(" expr ")" | STRING | field | field OP value
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "expr.h"

namespace mediagrep {

struct ParseError : std::runtime_error {
    std::size_t offset;  // byte offset into the expression text
    ParseError(std::size_t off, const std::string& message)
        : std::runtime_error(message), offset(off) {}
};

// Throws ParseError.
ExprPtr parse(std::string_view text);

}  // namespace mediagrep
