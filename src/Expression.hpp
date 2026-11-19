#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace varyn {

// The values an expression can read while it is being evaluated.
struct ExpressionContext {
    double time = 0.0;       // seconds since start           -> `time`, `t`
    double deltaTime = 0.0;  // seconds since previous frame   -> `dt`
    double frame = 0.0;      // frame counter                  -> `frame`
    double index = 0.0;      // instance index inside a model  -> `i`
    double count = 1.0;      // number of instances            -> `n`
};

class ExpressionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A small math expression such as "sin(time * 2) * 0.5 + 0.5".
//
// Supported syntax:
//   numbers      1, 2.5, .5, 1e-3
//   variables    time (t), dt, frame, i, n, pi, tau, e
//   operators    + - * / % ^ (power), unary minus, parentheses
//   functions    sin cos tan asin acos atan sqrt abs floor ceil fract round
//                sign exp log radians degrees
//                min max pow mod atan2 step
//                clamp mix smoothstep
//
// Parsing happens once; evaluation is a cheap tree walk every frame.
class Expression {
public:
    Expression();                          // the constant 0
    Expression(double constantValue);      // NOLINT: implicit on purpose

    static Expression parse(std::string_view source);  // throws ExpressionError

    double evaluate(const ExpressionContext& context) const;

    // True when the expression does not depend on any variable.
    bool isConstant() const;

    const std::string& source() const { return source_; }

    struct Node;  // defined in Expression.cpp

private:
    std::shared_ptr<const Node> root_;
    std::string source_;
};

}  // namespace varyn
