#include "Expression.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>

namespace varyn {

// ---------------------------------------------------------------------------
// Syntax tree
// ---------------------------------------------------------------------------

namespace {

enum class Variable { Time, DeltaTime, Frame, Index, Count };

using Function1 = double (*)(double);
using Function2 = double (*)(double, double);
using Function3 = double (*)(double, double, double);

struct FunctionInfo {
    const char* name;
    int arity;
    Function1 f1 = nullptr;
    Function2 f2 = nullptr;
    Function3 f3 = nullptr;
};

double fract(double x) { return x - std::floor(x); }
double sign(double x) { return (x > 0.0) - (x < 0.0); }
double radians(double x) { return x * std::numbers::pi / 180.0; }
double degrees(double x) { return x * 180.0 / std::numbers::pi; }
double glslMod(double x, double y) { return x - y * std::floor(x / y); }
double step(double edge, double x) { return x < edge ? 0.0 : 1.0; }
double clamp(double x, double lo, double hi) { return std::fmin(std::fmax(x, lo), hi); }
double mix(double a, double b, double t) { return a + (b - a) * t; }
double smoothstep(double e0, double e1, double x) {
    double t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// clang-format off
const FunctionInfo kFunctions[] = {
    {"sin",   1, [](double x) { return std::sin(x); }},
    {"cos",   1, [](double x) { return std::cos(x); }},
    {"tan",   1, [](double x) { return std::tan(x); }},
    {"asin",  1, [](double x) { return std::asin(x); }},
    {"acos",  1, [](double x) { return std::acos(x); }},
    {"atan",  1, [](double x) { return std::atan(x); }},
    {"sqrt",  1, [](double x) { return std::sqrt(x); }},
    {"abs",   1, [](double x) { return std::fabs(x); }},
    {"floor", 1, [](double x) { return std::floor(x); }},
    {"ceil",  1, [](double x) { return std::ceil(x); }},
    {"round", 1, [](double x) { return std::round(x); }},
    {"exp",   1, [](double x) { return std::exp(x); }},
    {"log",   1, [](double x) { return std::log(x); }},
    {"fract",   1, fract},
    {"sign",    1, sign},
    {"radians", 1, radians},
    {"degrees", 1, degrees},
    {"min",   2, nullptr, [](double a, double b) { return std::fmin(a, b); }},
    {"max",   2, nullptr, [](double a, double b) { return std::fmax(a, b); }},
    {"pow",   2, nullptr, [](double a, double b) { return std::pow(a, b); }},
    {"atan2", 2, nullptr, [](double y, double x) { return std::atan2(y, x); }},
    {"mod",   2, nullptr, glslMod},
    {"step",  2, nullptr, step},
    {"clamp",      3, nullptr, nullptr, clamp},
    {"mix",        3, nullptr, nullptr, mix},
    {"smoothstep", 3, nullptr, nullptr, smoothstep},
};
// clang-format on

}  // namespace

struct Expression::Node {
    enum class Kind { Number, Variable, Negate, Binary, Call };

    Kind kind = Kind::Number;
    double number = 0.0;                        // Kind::Number
    Variable variable = Variable::Time;         // Kind::Variable
    char op = 0;                                // Kind::Binary
    const FunctionInfo* function = nullptr;     // Kind::Call
    std::vector<std::shared_ptr<const Node>> children;
};

using NodePtr = std::shared_ptr<const Expression::Node>;
using Node = Expression::Node;

// ---------------------------------------------------------------------------
// Recursive descent parser
//
//   expression := term   (('+' | '-') term)*
//   term       := unary  (('*' | '/' | '%') unary)*
//   unary      := ('-' | '+') unary | power
//   power      := primary ('^' unary)?
//   primary    := number | identifier | identifier '(' args ')' | '(' expression ')'
// ---------------------------------------------------------------------------

namespace {

NodePtr makeNumber(double value) {
    auto node = std::make_shared<Node>();
    node->kind = Node::Kind::Number;
    node->number = value;
    return node;
}

NodePtr makeVariable(Variable variable) {
    auto node = std::make_shared<Node>();
    node->kind = Node::Kind::Variable;
    node->variable = variable;
    return node;
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    NodePtr parseAll() {
        NodePtr result = parseExpression();
        skipWhitespace();
        if (!atEnd()) fail("unexpected '" + std::string(1, peek()) + "'");
        return result;
    }

private:
    NodePtr parseExpression() {
        NodePtr left = parseTerm();
        while (true) {
            char op = peekOperator();
            if (op != '+' && op != '-') return left;
            ++position_;
            left = makeBinary(op, left, parseTerm());
        }
    }

    NodePtr parseTerm() {
        NodePtr left = parseUnary();
        while (true) {
            char op = peekOperator();
            if (op != '*' && op != '/' && op != '%') return left;
            ++position_;
            left = makeBinary(op, left, parseUnary());
        }
    }

    NodePtr parseUnary() {
        char op = peekOperator();
        if (op == '+') {
            ++position_;
            return parseUnary();
        }
        if (op == '-') {
            ++position_;
            auto node = std::make_shared<Node>();
            node->kind = Node::Kind::Negate;
            node->children.push_back(parseUnary());
            return node;
        }
        return parsePower();
    }

    NodePtr parsePower() {
        NodePtr base = parsePrimary();
        if (peekOperator() == '^') {
            ++position_;
            return makeBinary('^', base, parseUnary());  // right associative
        }
        return base;
    }

    NodePtr parsePrimary() {
        skipWhitespace();
        if (atEnd()) fail("unexpected end of expression");

        char c = peek();
        if (c == '(') {
            ++position_;
            NodePtr inner = parseExpression();
            expect(')');
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return parseNumber();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return parseIdentifier();

        fail("unexpected '" + std::string(1, c) + "'");
    }

    NodePtr parseNumber() {
        const char* begin = text_.data() + position_;
        char* end = nullptr;
        double value = std::strtod(begin, &end);
        if (end == begin) fail("invalid number");
        position_ += static_cast<size_t>(end - begin);
        return makeNumber(value);
    }

    NodePtr parseIdentifier() {
        size_t start = position_;
        while (!atEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            ++position_;
        }
        std::string name(text_.substr(start, position_ - start));

        skipWhitespace();
        if (!atEnd() && peek() == '(') return parseCall(name);

        if (name == "time" || name == "t") return makeVariable(Variable::Time);
        if (name == "dt") return makeVariable(Variable::DeltaTime);
        if (name == "frame") return makeVariable(Variable::Frame);
        if (name == "i") return makeVariable(Variable::Index);
        if (name == "n") return makeVariable(Variable::Count);
        if (name == "pi") return makeNumber(std::numbers::pi);
        if (name == "tau") return makeNumber(2.0 * std::numbers::pi);
        if (name == "e") return makeNumber(std::numbers::e);

        fail("unknown variable '" + name + "'");
    }

    NodePtr parseCall(const std::string& name) {
        const FunctionInfo* function = nullptr;
        for (const FunctionInfo& candidate : kFunctions) {
            if (name == candidate.name) function = &candidate;
        }
        if (!function) fail("unknown function '" + name + "'");

        auto node = std::make_shared<Node>();
        node->kind = Node::Kind::Call;
        node->function = function;

        expect('(');
        skipWhitespace();
        if (!atEnd() && peek() != ')') {
            node->children.push_back(parseExpression());
            while (peekOperator() == ',') {
                ++position_;
                node->children.push_back(parseExpression());
            }
        }
        expect(')');

        if (static_cast<int>(node->children.size()) != function->arity) {
            fail("'" + name + "' expects " + std::to_string(function->arity) + " argument(s), got " +
                 std::to_string(node->children.size()));
        }
        return node;
    }

    static NodePtr makeBinary(char op, NodePtr left, NodePtr right) {
        auto node = std::make_shared<Node>();
        node->kind = Node::Kind::Binary;
        node->op = op;
        node->children = {std::move(left), std::move(right)};
        return node;
    }

    // Helpers ---------------------------------------------------------------

    bool atEnd() const { return position_ >= text_.size(); }
    char peek() const { return text_[position_]; }

    void skipWhitespace() {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(peek()))) ++position_;
    }

    // Returns the next non-whitespace character without consuming it, or 0.
    char peekOperator() {
        skipWhitespace();
        return atEnd() ? 0 : peek();
    }

    void expect(char c) {
        skipWhitespace();
        if (atEnd() || peek() != c) fail(std::string("expected '") + c + "'");
        ++position_;
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw ExpressionError("in expression \"" + std::string(text_) + "\" at column " +
                              std::to_string(position_ + 1) + ": " + message);
    }

    std::string_view text_;
    size_t position_ = 0;
};

double evaluateNode(const Node& node, const ExpressionContext& context) {
    switch (node.kind) {
        case Node::Kind::Number:
            return node.number;

        case Node::Kind::Variable:
            switch (node.variable) {
                case Variable::Time: return context.time;
                case Variable::DeltaTime: return context.deltaTime;
                case Variable::Frame: return context.frame;
                case Variable::Index: return context.index;
                case Variable::Count: return context.count;
            }
            return 0.0;

        case Node::Kind::Negate:
            return -evaluateNode(*node.children[0], context);

        case Node::Kind::Binary: {
            double a = evaluateNode(*node.children[0], context);
            double b = evaluateNode(*node.children[1], context);
            switch (node.op) {
                case '+': return a + b;
                case '-': return a - b;
                case '*': return a * b;
                case '/': return a / b;
                case '%': return glslMod(a, b);
                case '^': return std::pow(a, b);
            }
            return 0.0;
        }

        case Node::Kind::Call: {
            const FunctionInfo& f = *node.function;
            auto arg = [&](size_t i) { return evaluateNode(*node.children[i], context); };
            switch (f.arity) {
                case 1: return f.f1(arg(0));
                case 2: return f.f2(arg(0), arg(1));
                case 3: return f.f3(arg(0), arg(1), arg(2));
            }
            return 0.0;
        }
    }
    return 0.0;
}

bool dependsOnVariables(const Node& node) {
    if (node.kind == Node::Kind::Variable) return true;
    for (const NodePtr& child : node.children) {
        if (dependsOnVariables(*child)) return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Expression
// ---------------------------------------------------------------------------

Expression::Expression() : Expression(0.0) {}

Expression::Expression(double constantValue)
    : root_(makeNumber(constantValue)), source_(std::to_string(constantValue)) {}

Expression Expression::parse(std::string_view source) {
    Expression expression;
    expression.root_ = Parser(source).parseAll();
    expression.source_ = std::string(source);
    return expression;
}

double Expression::evaluate(const ExpressionContext& context) const {
    return evaluateNode(*root_, context);
}

bool Expression::isConstant() const { return !dependsOnVariables(*root_); }

}  // namespace varyn
