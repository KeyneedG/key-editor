#include "eryscript/eryscript.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>

namespace eryscript {

Value::Value(Function function) : data(std::make_shared<Callable>(Callable{std::move(function), false})) {}
bool Value::isNull() const { return std::holds_alternative<std::monostate>(data); }
double Value::number() const {
    if (const auto* n = std::get_if<double>(&data)) return *n;
    if (const auto* b = std::get_if<bool>(&data)) return *b ? 1 : 0;
    if (isNull()) return 0;
    if (const auto* s = std::get_if<std::string>(&data)) {
        std::istringstream stream(*s);
        stream.imbue(std::locale::classic());
        double result;
        if (stream >> result) { stream >> std::ws; if (stream.eof() && std::isfinite(result)) return result; }
    }
    throw std::runtime_error("Expected a number");
}
bool Value::boolean() const {
    if (isNull()) return false;
    if (const auto* b = std::get_if<bool>(&data)) return *b;
    if (const auto* n = std::get_if<double>(&data)) return *n != 0;
    if (const auto* s = std::get_if<std::string>(&data)) {
        std::string lower = *s;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "true") return true;
        if (lower == "false") return false;
        throw std::runtime_error("Expected true or false");
    }
    return true;
}
std::string Value::string() const {
    if (isNull()) return "";
    if (const auto* s = std::get_if<std::string>(&data)) return *s;
    if (const auto* b = std::get_if<bool>(&data)) return *b ? "true" : "false";
    if (const auto* n = std::get_if<double>(&data)) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(15) << *n;
        return stream.str();
    }
    if (std::holds_alternative<std::shared_ptr<Array>>(data)) return "[array]";
    if (std::holds_alternative<std::shared_ptr<Callable>>(data)) return "[method]";
    return "[object]";
}
std::shared_ptr<Object> Value::object() const {
    if (const auto* p = std::get_if<std::shared_ptr<Object>>(&data)) if (*p) return *p;
    throw std::runtime_error("Expected an object");
}
std::shared_ptr<Array> Value::array() const {
    if (const auto* p = std::get_if<std::shared_ptr<Array>>(&data)) if (*p) return *p;
    throw std::runtime_error("Expected an array");
}
std::shared_ptr<Callable> Value::callable() const {
    if (const auto* p = std::get_if<std::shared_ptr<Callable>>(&data)) if (*p) return *p;
    throw std::runtime_error("Expected a method");
}
void Arguments::requireCount(std::size_t count) const {
    if (values.size() != count) throw std::runtime_error("Expected " + std::to_string(count) + " arguments");
}
Value Object::get(const std::string& name) const {
    if (const auto property = properties.find(name); property != properties.end()) return property->second.get();
    if (const auto field = fields.find(name); field != fields.end()) return field->second;
    throw std::runtime_error("Member not found: " + name);
}
void Object::set(const std::string& name, const Value& value) {
    if (const auto property = properties.find(name); property != properties.end()) {
        if (!property->second.set) throw std::runtime_error("Read-only member: " + name);
        property->second.set(value);
    } else if (const auto field = fields.find(name); field != fields.end()) field->second = value;
    else throw std::runtime_error("Member not found: " + name);
}
void Object::property(std::string name, std::function<Value()> get, std::function<void(const Value&)> set) {
    properties[std::move(name)] = {std::move(get), std::move(set)};
}
Error::Error(std::size_t line, const std::string& message)
    : std::runtime_error("Line " + std::to_string(line) + ": " + message), line(line) {}

namespace {
std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
enum class TokenKind { Word, Number, String, Symbol, Newline, End };
struct Token { TokenKind kind; std::string text; std::size_t line; };
std::vector<Token> tokenize(const std::string& source) {
    std::vector<Token> tokens;
    std::size_t i = 0, line = 1;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto letter = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 128; };
    while (i < source.size()) {
        const char c = source[i];
        if (c == '\n' || c == ';') { tokens.push_back({TokenKind::Newline, "", line}); if (c == '\n') ++line; ++i; }
        else if (std::isspace(static_cast<unsigned char>(c))) ++i;
        else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') ++i;
        } else if (c == '"') {
            const std::size_t startLine = line;
            std::string text;
            ++i;
            while (i < source.size() && source[i] != '"') {
                char next = source[i++];
                if (next == '\n') throw Error(startLine, "Unclosed string");
                if (next == '\\') {
                    if (i == source.size()) throw Error(startLine, "Unclosed string escape");
                    next = source[i++];
                    if (next == 'n') next = '\n'; else if (next == 'r') next = '\r';
                    else if (next == 't') next = '\t'; else if (next == '0') next = '\0';
                }
                text += next;
            }
            if (i == source.size()) throw Error(startLine, "Unclosed string");
            ++i;
            tokens.push_back({TokenKind::String, std::move(text), startLine});
        } else if (digit(c) || (c == '.' && i + 1 < source.size() && digit(source[i + 1]))) {
            const std::size_t start = i;
            while (i < source.size() && digit(source[i])) ++i;
            if (i < source.size() && source[i] == '.') { ++i; while (i < source.size() && digit(source[i])) ++i; }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                ++i;
                if (i < source.size() && (source[i] == '+' || source[i] == '-')) ++i;
                if (i == source.size() || !digit(source[i])) throw Error(line, "Invalid exponent");
                while (i < source.size() && digit(source[i])) ++i;
            }
            tokens.push_back({TokenKind::Number, source.substr(start, i - start), line});
        } else if (letter(c)) {
            const std::size_t start = i++;
            while (i < source.size() && (letter(source[i]) || digit(source[i]))) ++i;
            tokens.push_back({TokenKind::Word, source.substr(start, i - start), line});
        } else {
            std::string symbol(1, c);
            if (i + 1 < source.size()) {
                const std::string two = source.substr(i, 2);
                if (two == "&&" || two == "||" || two == "==" || two == "!=" || two == "<=" || two == ">=") symbol = two;
            }
            if (std::string("+-*/%!=<>()[],.:").find(c) == std::string::npos && symbol != "&&" && symbol != "||") {
                throw Error(line, std::string("Unexpected character: ") + c);
            }
            i += symbol.size();
            tokens.push_back({TokenKind::Symbol, std::move(symbol), line});
        }
    }
    tokens.push_back({TokenKind::End, "", line});
    return tokens;
}

struct Symbols {
    std::unordered_map<std::string, std::size_t> slots;
    std::size_t slot(const std::string& name) {
        return slots.try_emplace(name, slots.size()).first->second;
    }
};

struct Expr;
struct Statement;
using Expression = std::shared_ptr<Expr>;
using Statements = std::vector<std::shared_ptr<Statement>>;
struct Expr {
    enum Kind { Literal, Name, Binary, Unary, Member, Index, Call, New, Assign, ByRef } kind;
    std::size_t line;
    std::string text;
    Value literal;
    Expression left, right;
    std::vector<Expression> args;
    std::vector<std::vector<Expression>> dimensions;
    std::vector<std::string> typeNames;
    bool declaresVariable = false;
    bool isThis = false;
    std::size_t slot = 0;
    unsigned treeDepth = 1;
    Expr(Kind kind, std::size_t line) : kind(kind), line(line) {}
};
void validateTree(const Expression& expression) {
    const auto child = [&](const Expression& value) {
        if (value) expression->treeDepth = std::max(expression->treeDepth, value->treeDepth + 1);
    };
    child(expression->left); child(expression->right);
    for (const auto& value : expression->args) child(value);
    for (const auto& group : expression->dimensions) for (const auto& value : group) child(value);
    if (expression->treeDepth > 128) throw Error(expression->line, "Expression tree is too deep");
}
struct Branch { Expression condition; Statements body; };
struct Statement {
    enum Kind { ExpressionStmt, Var, Method, If, For, Return, Break, Continue, Using, Specify } kind;
    std::size_t line;
    std::string name;
    Expression expression, to;
    Statements body;
    std::vector<std::size_t> parameters;
    std::size_t slot = 0;
    std::vector<Branch> branches;
    Statement(Kind kind, std::size_t line) : kind(kind), line(line) {}
};

class Parser {
    std::vector<Token> tokens_;
    Symbols& symbols_;
    std::size_t pos_ = 0;
    unsigned depth_ = 0;
    const Token& peek() const { return tokens_[pos_]; }
    bool is(const std::string& text) const { return peek().text == text; }
    bool word(const std::string& text) const { return peek().kind == TokenKind::Word && lower(peek().text) == text; }
    bool take(const std::string& text) { if (!is(text)) return false; ++pos_; return true; }
    void expect(const std::string& text) { if (!take(text)) throw Error(peek().line, "Expected '" + text + "'"); }
    std::string identifier() {
        if (peek().kind != TokenKind::Word) throw Error(peek().line, "Expected an identifier");
        return tokens_[pos_++].text;
    }
    void newlines() { while (peek().kind == TokenKind::Newline) ++pos_; }
    void endLine() {
        if (peek().kind != TokenKind::Newline && peek().kind != TokenKind::End) throw Error(peek().line, "Unexpected text at end of statement: " + peek().text);
        newlines();
    }
    std::string typeName() {
        if (++depth_ > 128) throw Error(peek().line, "Type nesting is too deep");
        std::string name = identifier();
        while (is(".") || is(":")) { ++pos_; name += "." + identifier(); }
        if (take("<")) {
            name += "<" + typeName();
            while (take(",")) name += "," + typeName();
            expect(">"); name += ">";
        }
        while (is("[")) {
            const auto saved = pos_++;
            std::string rank = "[";
            while (take(",")) rank += ",";
            if (!take("]")) { pos_ = saved; break; }
            name += rank + "]";
        }
        --depth_;
        return name;
    }
    std::vector<Expression> list(const std::string& end, bool arguments) {
        std::vector<Expression> result;
        newlines();
        if (!is(end)) {
            do {
                newlines();
                if (arguments && (word("ref") || word("out") || word("in"))) {
                    auto arg = std::make_shared<Expr>(Expr::ByRef, peek().line);
                    arg->text = lower(tokens_[pos_++].text);
                    if (arg->text == "out" && word("var")) {
                        ++pos_; arg->declaresVariable = true;
                        arg->left = std::make_shared<Expr>(Expr::Name, peek().line);
                        arg->left->text = identifier();
                        arg->left->slot = symbols_.slot(arg->left->text);
                        arg->left->isThis = lower(arg->left->text) == "this";
                    } else arg->left = expression();
                    validateTree(arg); result.push_back(std::move(arg));
                } else result.push_back(expression());
                newlines();
            } while (take(","));
        }
        expect(end);
        return result;
    }
    Expression primary() {
        while (word("call")) ++pos_; // Optional legacy prefix; does not add recursion.
        const Token token = tokens_[pos_++];
        auto node = std::make_shared<Expr>(Expr::Literal, token.line);
        if (token.kind == TokenKind::String) node->literal = token.text;
        else if (token.kind == TokenKind::Number) {
            try { node->literal = Value(token.text).number(); } catch (...) { throw Error(token.line, "Invalid number"); }
        } else if (token.text == "(") { node = expression(); expect(")"); }
        else if (token.kind == TokenKind::Word) {
            const std::string keyword = lower(token.text);
            if (keyword == "true" || keyword == "false") node->literal = keyword == "true";
            else if (keyword == "null") node->literal = {};
            else if (keyword == "new") {
                node->kind = Expr::New; node->text = typeName();
                if (take("[")) {
                    do {
                        auto lengths = list("]", false);
                        if (lengths.empty()) throw Error(token.line, "Array creation requires lengths");
                        node->dimensions.push_back(std::move(lengths));
                    } while (take("["));
                } else { expect("("); node->args = list(")", true); }
            } else {
                node->kind = Expr::Name; node->text = token.text;
                node->slot = symbols_.slot(token.text); node->isThis = keyword == "this";
            }
        } else if (token.text == "+" || token.text == "-" || token.text == "!") {
            node->kind = Expr::Unary; node->text = token.text; node->left = expression(7);
        } else throw Error(token.line, "Expected an expression");

        validateTree(node);
        while (true) {
            std::vector<std::string> types;
            if (is("<")) {
                const auto saved = pos_;
                const auto savedDepth = depth_;
                try {
                    ++pos_; types.push_back(typeName());
                    while (take(",")) types.push_back(typeName());
                    expect(">");
                    if (!is("(")) { types.clear(); pos_ = saved; }
                } catch (const Error&) { types.clear(); pos_ = saved; depth_ = savedDepth; }
            }
            if (take("(")) {
                auto call = std::make_shared<Expr>(Expr::Call, token.line);
                call->left = node; call->args = list(")", true); call->typeNames = std::move(types);
                node = std::move(call);
            } else if (take(".") || take(":")) {
                auto member = std::make_shared<Expr>(Expr::Member, token.line);
                member->left = node; member->text = identifier(); node = std::move(member);
            } else if (take("[")) {
                auto index = std::make_shared<Expr>(Expr::Index, token.line);
                index->left = node;
                do {
                    auto indices = list("]", false);
                    if (indices.empty()) throw Error(token.line, "An index cannot be empty");
                    index->dimensions.push_back(std::move(indices));
                } while (take("["));
                node = std::move(index);
            } else break;
            validateTree(node);
        }
        return node;
    }
    static int precedence(const Token& token) {
        if (token.kind != TokenKind::Symbol) return 0;
        const std::string& op = token.text;
        if (op == "=") return 1;
        if (op == "||") return 2;
        if (op == "&&") return 3;
        if (op == "==" || op == "!=" || op == ">" || op == "<" || op == ">=" || op == "<=") return 4;
        if (op == "+" || op == "-") return 5;
        if (op == "*" || op == "/" || op == "%") return 6;
        return 0;
    }
    Expression expression(int minimum = 1) {
        if (++depth_ > 128) throw Error(peek().line, "Expression nesting is too deep");
        auto left = primary();
        unsigned operators = 0;
        while (precedence(peek()) >= minimum) {
            if (++operators > 128) throw Error(peek().line, "Expression chain is too long");
            const Token op = tokens_[pos_++];
            auto node = std::make_shared<Expr>(op.text == "=" ? Expr::Assign : Expr::Binary, op.line);
            node->text = op.text; node->left = left;
            node->right = expression(precedence(op) + (op.text == "=" ? 0 : 1));
            validateTree(node);
            left = std::move(node);
        }
        --depth_;
        return left;
    }
    Statements block() {
        if (++depth_ > 128) throw Error(peek().line, "Block nesting is too deep");
        Statements statements;
        newlines();
        while (peek().kind != TokenKind::End && !word("end") && !word("mend") && !word("else") && !word("elseif")) {
            statements.push_back(statement());
        }
        --depth_;
        return statements;
    }
    std::shared_ptr<Statement> statement() {
        auto node = std::make_shared<Statement>(Statement::ExpressionStmt, peek().line);
        if (word("var")) {
            ++pos_; node->kind = Statement::Var; node->name = identifier();
            if (take("=")) node->expression = expression();
            endLine();
        } else if (word("using")) {
            ++pos_; node->kind = Statement::Using; node->name = identifier();
            while (take(".")) node->name += "." + identifier();
            endLine();
        } else if (word("specify")) {
            ++pos_; node->kind = Statement::Specify; node->name = lower(identifier());
            expect("="); node->expression = expression(); endLine();
        } else if (word("method")) {
            ++pos_; node->kind = Statement::Method; node->name = identifier();
            if (take("(")) {
                if (!is(")")) { do { node->parameters.push_back(symbols_.slot(identifier())); } while (take(",")); }
                expect(")");
            }
            endLine(); node->body = block();
            if (!word("mend")) throw Error(node->line, "Method '" + node->name + "' is missing mend");
            ++pos_; endLine();
        } else if (word("if")) {
            ++pos_; node->kind = Statement::If;
            do {
                auto condition = expression(); endLine();
                node->branches.push_back({condition, block()});
                if (!word("elseif")) break;
                ++pos_;
            } while (true);
            if (word("else")) { ++pos_; endLine(); node->branches.push_back({{}, block()}); }
            if (!word("end")) throw Error(node->line, "If is missing end");
            ++pos_; endLine();
        } else if (word("for")) {
            ++pos_; node->kind = Statement::For; node->name = identifier(); expect("=");
            node->expression = expression();
            if (!word("to")) throw Error(node->line, "For requires 'to'");
            ++pos_; node->to = expression(); endLine(); node->body = block();
            if (!word("end")) throw Error(node->line, "For is missing end");
            ++pos_; endLine();
        } else if (word("return")) {
            ++pos_; node->kind = Statement::Return;
            if (peek().kind != TokenKind::Newline && peek().kind != TokenKind::End) node->expression = expression();
            endLine();
        } else if (word("break") || word("continue")) {
            node->kind = word("break") ? Statement::Break : Statement::Continue;
            ++pos_; endLine();
        } else { node->expression = expression(); endLine(); }
        if (node->kind == Statement::Var || node->kind == Statement::For) node->slot = symbols_.slot(node->name);
        return node;
    }
public:
    Parser(const std::string& source, Symbols& symbols) : tokens_(tokenize(source)), symbols_(symbols) {}
    Statements parse() {
        auto statements = block();
        if (peek().kind != TokenKind::End) throw Error(peek().line, "Unexpected block terminator: " + peek().text);
        return statements;
    }
};

struct Scope {
    // Names are numbered once at load time. An empty slot differs from a declared null.
    std::vector<std::optional<Value>> variables;
    std::vector<std::size_t> declared;
    Scope* parent = nullptr;
    Value* find(std::size_t slot) {
        return slot < variables.size() && variables[slot] ? &*variables[slot] : nullptr;
    }
    void set(std::size_t slot, Value value) {
        if (slot >= variables.size()) variables.resize(slot + 1);
        if (!variables[slot]) declared.push_back(slot);
        variables[slot] = std::move(value);
    }
    void clear() {
        for (auto slot : declared) variables[slot].reset();
        declared.clear();
    }
};
struct ScopeGuard {
    Scope& scope;
    explicit ScopeGuard(Scope& scope) : scope(scope) { scope.clear(); }
    ~ScopeGuard() { scope.clear(); }
};

struct CallBuffer {
    Arguments args;
    std::vector<std::pair<std::size_t, std::function<void(Value)>>> setters;
    void clear() { args.values.clear(); args.modes.clear(); args.typeNames.clear(); setters.clear(); }
};
struct CallGuard {
    CallBuffer& buffer;
    explicit CallGuard(CallBuffer& buffer) : buffer(buffer) { buffer.clear(); }
    ~CallGuard() { buffer.clear(); } // Release native owners on success and on exceptions.
};
struct Location {
    std::function<Value()> get;
    std::function<void(Value)> set;
};
struct Flow {
    enum Kind { None, Return, Break, Continue } kind = None;
    Value value;
};

std::size_t indexNumber(const Value& value) {
    const double number = value.number();
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number || number > 1000000) {
        throw std::runtime_error("Index/length must be an integer between 0 and 1000000");
    }
    return static_cast<std::size_t>(number);
}
Value member(const Value& target, const std::string& name) {
    if (std::holds_alternative<std::shared_ptr<Object>>(target.data)) return target.object()->get(name);
    if (name == "ToString") return Value(Function([target](Arguments& args) { args.requireCount(0); return target.string(); }));
    if (std::holds_alternative<std::string>(target.data)) {
        const auto text = target.string();
        if (name == "Length" || name == "Count") return double(text.size());
        if (name == "Contains") return Value(Function([text](Arguments& args) { args.requireCount(1); return text.find(args.values[0].string()) != std::string::npos; }));
    }
    if (std::holds_alternative<std::shared_ptr<Array>>(target.data)) {
        const auto array = target.array();
        if (name == "Length" || name == "Count") return double(array->values.size());
        if (name == "Rank") return double(array->dimensions.empty() ? 1 : array->dimensions.size());
        if (name == "GetLength") return Value(Function([array](Arguments& args) {
            args.requireCount(1); const auto index = indexNumber(args.values[0]);
            if (array->dimensions.empty()) { if (index == 0) return Value(double(array->values.size())); }
            else if (index < array->dimensions.size()) return Value(double(array->dimensions[index]));
            throw std::runtime_error("Invalid array dimension");
        }));
        if (name == "Add" || name == "Clear") return Value(Function([array, name](Arguments& args) {
            if (!array->dimensions.empty()) throw std::runtime_error("A fixed array cannot change length");
            args.requireCount(name == "Add" ? 1 : 0);
            if (name == "Clear") array->values.clear();
            else { if (array->values.size() >= 1000000) throw std::runtime_error("List is too large"); array->values.push_back(args.values[0]); }
            return Value{};
        }));
    }
    throw std::runtime_error("Member not found: " + name);
}

struct State : std::enable_shared_from_this<State> {
    Symbols symbols;
    Statements program;
    Scope globals;
    Value root;
    Spec spec;
    std::unordered_map<std::string, std::shared_ptr<Statement>> methods;
    std::unordered_map<std::string, Value> methodValues;
    std::unordered_map<std::string, Function> constructors;
    std::vector<std::string> usings;
    bool stopped = false;
    std::size_t steps = 0, stepLimit = 100000, allocatedElements = 0;
    unsigned callDepth = 0;
    unsigned expressionDepth = 0;
    unsigned blockDepth = 0;
    // Each active expression gets its own buffer, including recursive calls/callbacks.
    std::array<CallBuffer, 48> callBuffers;
    std::array<Scope, 32> callScopes;

    void tick(std::size_t line) {
        if (stopped) throw Error(line, "Script is stopped");
        if (++steps > stepLimit) throw Error(line, "Script execution limit exceeded");
    }
    Value resolve(const std::string& name, std::size_t slot, Scope& scope);
    Location location(const Expression& expression, Scope& scope);
    Location indexed(const Expression& expression, Scope& scope);
    Value eval(const Expression& expression, Scope& scope);
    Flow execute(const Statements& statements, Scope& scope, unsigned loopDepth = 0);
    Value invoke(const std::string& name, Arguments& args);
    Value makeArray(const Expression& expression, Scope& scope, std::size_t dimension);
};

Value boundPath(const std::string& path, State& state) {
    const auto dot = path.find('.');
    const std::string first = path.substr(0, dot);
    const auto* bound = state.globals.find(state.symbols.slot(first));
    if (!bound) throw std::runtime_error("Unknown name: " + first);
    Value value = *bound;
    std::size_t start = dot;
    while (start != std::string::npos) {
        const auto end = path.find('.', start + 1);
        value = member(value, path.substr(start + 1, end == std::string::npos ? end : end - start - 1));
        start = end;
    }
    return value;
}
Value State::resolve(const std::string& name, std::size_t slot, Scope& scope) {
    for (Scope* current = &scope; current; current = current->parent) {
        if (const auto* value = current->find(slot)) return *value;
    }
    if (methods.count(name)) {
        if (const auto found = methodValues.find(name); found != methodValues.end()) return found->second;
        std::weak_ptr<State> weak = shared_from_this();
        Value result(Function([weak, name](Arguments& args) {
            const auto state = weak.lock();
            return state && !state->stopped && state->spec.enabled ? state->invoke(name, args) : Value{};
        }));
        result.callable()->scriptMethod = true;
        methodValues.emplace(name, result);
        return result;
    }
    if (const auto* object = std::get_if<std::shared_ptr<Object>>(&root.data)) {
        if (*object && ((*object)->fields.count(name) || (*object)->properties.count(name))) return (*object)->get(name);
    }
    for (const auto& ns : usings) {
        try { return boundPath(ns + "." + name, *this); } catch (const std::runtime_error&) {}
    }
    throw std::runtime_error("Unknown name: " + name);
}
Location State::location(const Expression& expression, Scope& scope) {
    if (expression->kind == Expr::Name) {
        const std::string name = expression->text;
        if (expression->isThis) throw std::runtime_error("Cannot assign this");
        const auto slot = expression->slot;
        for (Scope* current = &scope; current; current = current->parent) {
            if (current->find(slot)) return {
                [current, slot] { return *current->find(slot); },
                [current, slot](Value value) { *current->find(slot) = std::move(value); }};
        }
        if (const auto* object = std::get_if<std::shared_ptr<Object>>(&root.data)) {
            if (*object && ((*object)->fields.count(name) || (*object)->properties.count(name))) return {
                [object = *object, name] { return object->get(name); },
                [object = *object, name](Value value) { object->set(name, value); }};
        }
        throw std::runtime_error("Undefined variable: " + name);
    }
    if (expression->kind == Expr::Member) {
        const auto object = eval(expression->left, scope).object();
        const std::string name = expression->text;
        return {[object, name] { return object->get(name); },
            [object, name](Value value) { object->set(name, value); }};
    }
    if (expression->kind == Expr::Index) return indexed(expression, scope);
    throw std::runtime_error("Assignment/ref/out requires a variable, member or index");
}
Location State::indexed(const Expression& expression, Scope& scope) {
    Value target = eval(expression->left, scope);
    Location result;
    for (std::size_t group = 0; group < expression->dimensions.size();) {
        std::vector<Value> indices;
        std::size_t consumed = 1;
        if (const auto* a = std::get_if<std::shared_ptr<Array>>(&target.data)) {
            const auto rank = (*a)->dimensions.size();
            if (rank > 1 && expression->dimensions[group].size() == 1 && group + rank <= expression->dimensions.size()) {
                bool single = true;
                for (std::size_t i = 0; i < rank; ++i) single = single && expression->dimensions[group + i].size() == 1;
                if (single) consumed = rank;
            }
        }
        for (std::size_t i = 0; i < consumed; ++i) {
            for (const auto& index : expression->dimensions[group + i]) indices.push_back(eval(index, scope));
        }
        if (std::holds_alternative<std::shared_ptr<Array>>(target.data)) {
            const auto array = target.array();
            const auto rank = array->dimensions.empty() ? 1 : array->dimensions.size();
            if (indices.size() != rank) throw std::runtime_error("Incorrect number of array indices");
            std::size_t offset = 0;
            for (std::size_t i = 0; i < rank; ++i) {
                const auto index = indexNumber(indices[i]);
                const auto length = array->dimensions.empty() ? array->values.size() : array->dimensions[i];
                if (index >= length) throw std::runtime_error("Array index is out of bounds");
                offset = offset * length + index;
            }
            result = {[array, offset] { return array->values.at(offset); },
                [array, offset](Value value) { array->values.at(offset) = std::move(value); }};
        } else if (std::holds_alternative<std::shared_ptr<Object>>(target.data)) {
            if (indices.size() != 1) throw std::runtime_error("Object indexing requires one key");
            const auto object = target.object();
            const auto key = indices[0].string();
            result = {[object, key] { return object->get(key); },
                [object, key](Value value) {
                    if (object->properties.count(key)) object->set(key, value);
                    else object->fields[key] = std::move(value);
                }};
        } else if (std::holds_alternative<std::string>(target.data)) {
            if (indices.size() != 1) throw std::runtime_error("String indexing requires one index");
            const auto text = target.string(); const auto index = indexNumber(indices[0]);
            if (index >= text.size()) throw std::runtime_error("String index is out of bounds");
            result = {[text, index] { return Value(std::string(1, text[index])); },
                [](Value) { throw std::runtime_error("Strings are read-only"); }};
        } else throw std::runtime_error("Value cannot be indexed");
        group += consumed;
        if (group < expression->dimensions.size()) target = result.get();
    }
    return result;
}
Value State::makeArray(const Expression& expression, Scope& scope, std::size_t dimension) {
    tick(expression->line);
    const auto base = expression->text.substr(0, expression->text.find_first_of("<["));
    bool knownType = constructors.count(base) != 0;
    for (const auto& ns : usings) knownType = knownType || constructors.count(ns + "." + base) != 0;
    if (!knownType) throw std::runtime_error("Array element type is not registered: " + expression->text);
    auto array = std::make_shared<Array>();
    std::size_t count = 1;
    for (const auto& length : expression->dimensions[dimension]) {
        const auto size = indexNumber(eval(length, scope));
        if (size && count > 1000000 / size) throw std::runtime_error("Array is too large");
        count *= size; array->dimensions.push_back(size);
    }
    if (count > 1000000 - allocatedElements) throw std::runtime_error("Array allocation limit exceeded");
    allocatedElements += count;
    Value initial;
    const auto type = lower(expression->text);
    if (type == "bool") initial = false;
    else if (type == "int" || type == "float" || type == "double" || type == "byte" || type == "long" ||
        type == "short" || type == "uint" || type == "ulong" || type == "ushort" || type == "sbyte" || type == "decimal") initial = 0;
    else if (type == "char") initial = std::string(1, '\0');
    array->values.resize(count, initial);
    if (dimension + 1 < expression->dimensions.size()) {
        for (auto& value : array->values) value = makeArray(expression, scope, dimension + 1);
    }
    return array;
}
Value State::eval(const Expression& expression, Scope& scope) {
    if (!expression) return {};
    if (expressionDepth >= 48) throw Error(expression->line, "Expression recursion limit exceeded");
    struct Guard { unsigned& depth; explicit Guard(unsigned& depth) : depth(depth) { ++depth; } ~Guard() { --depth; } } guard(expressionDepth);
    tick(expression->line);
    try {
        switch (expression->kind) {
        case Expr::Literal: return expression->literal;
        case Expr::Name: return expression->isThis ? root : resolve(expression->text, expression->slot, scope);
        case Expr::Member: return member(eval(expression->left, scope), expression->text);
        case Expr::Index: return indexed(expression, scope).get();
        case Expr::Assign: {
            Value value = eval(expression->right, scope);
            location(expression->left, scope).set(value);
            return value;
        }
        case Expr::Unary: {
            Value value = eval(expression->left, scope);
            if (expression->text == "!") return !value.boolean();
            return expression->text == "-" ? -value.number() : value.number();
        }
        case Expr::Binary: {
            const auto& op = expression->text;
            const Value left = eval(expression->left, scope);
            if (op == "&&") return left.boolean() && eval(expression->right, scope).boolean();
            if (op == "||") return left.boolean() || eval(expression->right, scope).boolean();
            const Value right = eval(expression->right, scope);
            if (op == "==" || op == "!=") {
                bool equal = left.data == right.data;
                const auto a = std::get_if<std::shared_ptr<Object>>(&left.data);
                const auto b = std::get_if<std::shared_ptr<Object>>(&right.data);
                if (a && *a && (*a)->native && b && *b && (*b)->native) equal = (*a)->native->equals(*(*b)->native);
                return equal == (op == "==");
            }
            if (op == "+" && (std::holds_alternative<std::string>(left.data) || std::holds_alternative<std::string>(right.data))) return left.string() + right.string();
            const double a = left.number(), b = right.number();
            if (op == "<") return a < b;
            if (op == ">") return a > b;
            if (op == "<=") return a <= b;
            if (op == ">=") return a >= b;
            if ((op == "/" || op == "%") && b == 0) throw std::runtime_error("Division by zero");
            const double result = op == "+" ? a + b : op == "-" ? a - b : op == "*" ? a * b : op == "/" ? a / b : std::fmod(a, b);
            if (!std::isfinite(result)) throw std::runtime_error("Arithmetic result is not finite");
            return result;
        }
        case Expr::Call: {
            const auto callable = eval(expression->left, scope).callable();
            CallGuard call{callBuffers[expressionDepth - 1]};
            auto& args = call.buffer.args;
            args.typeNames = expression->typeNames;
            args.values.reserve(expression->args.size());
            args.modes.reserve(expression->args.size());
            for (const auto& argument : expression->args) {
                ArgumentMode mode = ArgumentMode::Value;
                std::function<void(Value)> setter;
                Value value;
                if (argument->kind == Expr::ByRef) {
                    mode = argument->text == "ref" ? ArgumentMode::Ref : argument->text == "out" ? ArgumentMode::Out : ArgumentMode::In;
                    if (argument->declaresVariable) scope.set(argument->left->slot, {});
                    if (mode == ArgumentMode::Ref || mode == ArgumentMode::Out) {
                        Location target = location(argument->left, scope); setter = target.set;
                        if (mode == ArgumentMode::Ref) value = target.get();
                    } else value = eval(argument->left, scope);
                } else value = eval(argument, scope);
                if (setter) call.buffer.setters.emplace_back(args.values.size(), std::move(setter));
                args.values.push_back(std::move(value)); args.modes.push_back(mode);
            }
            if (callable->scriptMethod && (!args.typeNames.empty() || std::any_of(args.modes.begin(), args.modes.end(),
                [](ArgumentMode mode) { return mode != ArgumentMode::Value; }))) throw std::runtime_error("Generic/ref/out/in arguments require a native binding");
            const auto result = callable->function(args);
            if (args.values.size() != expression->args.size()) throw std::runtime_error("Native binding changed the argument count");
            for (const auto& [index, setter] : call.buffer.setters) setter(args.values[index]);
            return result;
        }
        case Expr::New: {
            if (!expression->dimensions.empty()) return makeArray(expression, scope, 0);
            std::string type = expression->text;
            CallGuard call{callBuffers[expressionDepth - 1]};
            auto& args = call.buffer.args;
            args.values.reserve(expression->args.size());
            args.modes.reserve(expression->args.size());
            const auto generic = type.find('<');
            if (generic != std::string::npos) {
                unsigned depth = 0; std::size_t start = generic + 1;
                for (std::size_t i = start; i + 1 < type.size(); ++i) {
                    if (type[i] == '<') ++depth; else if (type[i] == '>') --depth;
                    else if (type[i] == ',' && !depth) { args.typeNames.push_back(type.substr(start, i - start)); start = i + 1; }
                }
                args.typeNames.push_back(type.substr(start, type.size() - start - 1));
                type = type.substr(0, generic);
            }
            auto constructor = constructors.find(expression->text);
            if (constructor == constructors.end()) constructor = constructors.find(type);
            if (constructor == constructors.end()) for (const auto& ns : usings) {
                constructor = constructors.find(ns + "." + type); if (constructor != constructors.end()) break;
            }
            if (constructor == constructors.end()) throw std::runtime_error("Constructor is not registered: " + expression->text);
            for (const auto& arg : expression->args) {
                if (arg->kind == Expr::ByRef) throw std::runtime_error("Constructor arguments cannot use ref/out/in");
                args.values.push_back(eval(arg, scope)); args.modes.push_back(ArgumentMode::Value);
            }
            return constructor->second(args);
        }
        case Expr::ByRef: throw std::runtime_error("ref/out/in is only valid in an argument list");
        }
    } catch (const Error&) { throw; }
    catch (const std::exception& error) { throw Error(expression->line, error.what()); }
    throw Error(expression->line, "Unknown expression");
}
Flow State::execute(const Statements& statements, Scope& scope, unsigned loopDepth) {
    if (blockDepth >= 48) throw Error(statements.empty() ? 1 : statements.front()->line, "Block recursion limit exceeded");
    struct Guard { unsigned& depth; explicit Guard(unsigned& depth) : depth(depth) { ++depth; } ~Guard() { --depth; } } guard(blockDepth);
    for (const auto& statement : statements) {
        tick(statement->line);
        try {
            switch (statement->kind) {
            case Statement::ExpressionStmt: eval(statement->expression, scope); break;
            case Statement::Var: scope.set(statement->slot, eval(statement->expression, scope)); break;
            case Statement::Method: methods[statement->name] = statement; break;
            case Statement::Using: usings.push_back(statement->name); break;
            case Statement::Specify: break; // Applied before executing the top-level body.
            case Statement::Return: return {Flow::Return, eval(statement->expression, scope)};
            case Statement::Break: case Statement::Continue:
                if (!loopDepth) throw Error(statement->line, "break/continue requires a for loop");
                return {statement->kind == Statement::Break ? Flow::Break : Flow::Continue, {}};
            case Statement::If:
                for (const auto& branch : statement->branches) {
                    if (!branch.condition || eval(branch.condition, scope).boolean()) {
                        const Flow flow = execute(branch.body, scope, loopDepth);
                        if (flow.kind != Flow::None) return flow;
                        break;
                    }
                }
                break;
            case Statement::For: {
                const double from = eval(statement->expression, scope).number(), to = eval(statement->to, scope).number();
                if (!std::isfinite(from) || !std::isfinite(to) || std::abs(from) > 9007199254740991.0 || std::abs(to) > 9007199254740991.0) {
                    throw Error(statement->line, "For range is not a finite, exactly representable integer range");
                }
                Scope iteration; iteration.parent = &scope;
                for (double i = std::round(from); i <= std::round(to); ++i) {
                    tick(statement->line);
                    iteration.clear(); iteration.set(statement->slot, i);
                    const Flow flow = execute(statement->body, iteration, loopDepth + 1);
                    if (flow.kind == Flow::Return) return flow;
                    if (flow.kind == Flow::Break) break;
                }
                break;
            }
            }
        } catch (const Error&) { throw; }
        catch (const std::exception& error) { throw Error(statement->line, error.what()); }
    }
    return {};
}
Value State::invoke(const std::string& name, Arguments& args) {
    if (stopped || !spec.enabled) return {};
    const auto found = methods.find(name);
    if (found == methods.end()) throw std::runtime_error("Method not found: " + name);
    const auto method = found->second;
    if (args.values.size() != method->parameters.size()) throw Error(method->line,
        "Method " + name + " expects " + std::to_string(method->parameters.size()) + " arguments");
    if (callDepth >= 32) throw Error(method->line, "Method recursion limit exceeded");
    if (!callDepth) { steps = 0; allocatedElements = 0; }
    struct Guard { unsigned& depth; explicit Guard(unsigned& depth) : depth(depth) { ++depth; } ~Guard() { --depth; } } guard(callDepth);
    ScopeGuard frame{callScopes[callDepth - 1]};
    Scope& local = frame.scope; local.parent = &globals;
    for (std::size_t i = 0; i < args.values.size(); ++i) local.set(method->parameters[i], args.values[i]);
    const Flow flow = execute(method->body, local);
    return flow.kind == Flow::Return ? flow.value : Value{};
}

std::shared_ptr<Object> mathObject() {
    auto math = std::make_shared<Object>();
    math->fields["PI"] = 3.14159265358979323846;
    const auto unary = [&](const char* name, double (*function)(double)) {
        math->fields[name] = Value(Function([function](Arguments& args) {
            args.requireCount(1); const double result = function(args.values[0].number());
            if (!std::isfinite(result)) throw std::runtime_error("Math result is not finite");
            return Value(result);
        }));
    };
    unary("Sin", std::sin); unary("Cos", std::cos); unary("Tan", std::tan); unary("Abs", std::fabs);
    unary("Sqrt", std::sqrt); unary("Floor", std::floor); unary("Ceiling", std::ceil); unary("Round", std::round);
    math->fields["Min"] = Value(Function([](Arguments& args) { args.requireCount(2); return Value(std::min(args.values[0].number(), args.values[1].number())); }));
    math->fields["Max"] = Value(Function([](Arguments& args) { args.requireCount(2); return Value(std::max(args.values[0].number(), args.values[1].number())); }));
    math->fields["Clamp"] = Value(Function([](Arguments& args) {
        args.requireCount(3); const double low = args.values[1].number(), high = args.values[2].number();
        if (low > high) throw std::runtime_error("Clamp minimum exceeds maximum");
        return Value(std::clamp(args.values[0].number(), low, high));
    }));
    return math;
}
void applySpec(State& state) {
    for (const auto& statement : state.program) {
        if (statement->kind != Statement::Specify) continue;
        if (statement->expression->kind != Expr::Literal) throw Error(statement->line, "specify requires a literal");
        const auto& value = statement->expression->literal;
        const auto& key = statement->name;
        try {
            if (key == "enabled") state.spec.enabled = value.boolean();
            else if (key == "readonly") state.spec.readOnly = value.boolean();
            else if (key == "name") state.spec.name = value.string();
            else if (key == "id") state.spec.id = value.string();
            else throw Error(statement->line, "Unknown specify key: " + key);
        } catch (const Error&) { throw; }
        catch (const std::exception& error) { throw Error(statement->line, error.what()); }
    }
}
} // namespace

struct Runtime::Impl {
    std::shared_ptr<State> state = std::make_shared<State>();
    std::unordered_map<std::string, Value> bindings;
    std::unordered_map<std::string, Function> constructors;
    std::string source;
    Impl() {
        static std::atomic_size_t nextId{0};
        state->spec.id = "ery-" + std::to_string(++nextId);
        state->spec.name = state->spec.id;
    }
};
Runtime::Runtime() : impl_(std::make_unique<Impl>()) {
    const auto math = mathObject();
    bind("Math", math); bind("Mathf", math); // Mathf is a compatibility alias, not a Unity dependency.
    auto console = std::make_shared<Object>();
    const Value log(Function([](Arguments& args) { args.requireCount(1); std::cout << args.values[0].string() << '\n'; return Value{}; }));
    console->fields["Log"] = log; console->fields["WriteLine"] = log;
    bind("Console", console); bind("Debug", console);
    auto system = std::make_shared<Object>(); system->fields["Math"] = math; system->fields["Console"] = console;
    bind("System", system);
    for (const std::string type : {"int", "float", "double", "byte", "sbyte", "short", "ushort", "uint", "long", "ulong", "decimal"}) {
        constructor(type, [type](Arguments& args) {
            args.requireCount(1); const double number = args.values[0].number();
            return Value(type == "float" || type == "double" || type == "decimal" ? number : std::trunc(number));
        });
    }
    constructor("bool", [](Arguments& args) { args.requireCount(1); return Value(args.values[0].boolean()); });
    constructor("string", [](Arguments& args) { args.requireCount(1); return Value(args.values[0].string()); });
    constructor("char", [](Arguments& args) {
        args.requireCount(1); const auto text = args.values[0].string();
        if (text.size() != 1) throw std::runtime_error("char requires one byte");
        return Value(text);
    });
    constructor("object", [](Arguments& args) { args.requireCount(0); return Value(std::make_shared<Object>()); });
    const Function list = [](Arguments& args) { args.requireCount(0); return Value(std::make_shared<Array>()); };
    const Function dictionary = [](Arguments& args) { args.requireCount(0); return Value(std::make_shared<Object>()); };
    constructor("List", list); constructor("System.Collections.Generic.List", list);
    constructor("Dictionary", dictionary); constructor("System.Collections.Generic.Dictionary", dictionary);
}
Runtime::~Runtime() { if (impl_) stop(); }
Runtime::Runtime(Runtime&&) noexcept = default;
Runtime& Runtime::operator=(Runtime&& other) noexcept {
    if (this != &other) { if (impl_) stop(); impl_ = std::move(other.impl_); stepLimit = other.stepLimit; }
    return *this;
}
void Runtime::bind(std::string name, Value value) {
    impl_->bindings[name] = value; impl_->state->globals.set(impl_->state->symbols.slot(name), std::move(value));
}
void Runtime::constructor(std::string name, Function function) {
    impl_->constructors[name] = function; impl_->state->constructors[std::move(name)] = std::move(function);
}
void Runtime::load(const std::string& source, Value root) {
    if (impl_->state->spec.readOnly && source != impl_->source) throw std::runtime_error("Script is read-only");
    auto state = std::make_shared<State>();
    state->spec.id = impl_->state->spec.id; state->spec.name = state->spec.id;
    // Invalidate old callbacks even if the new source fails to parse.
    stop();
    impl_->state = state; impl_->source = source;
    try {
        state->program = Parser(source, state->symbols).parse();
        state->root = std::move(root);
        for (const auto& [name, value] : impl_->bindings) state->globals.set(state->symbols.slot(name), value);
        state->constructors = impl_->constructors; state->stepLimit = stepLimit;
        applySpec(*state);
        state->callDepth = 1;
        if (state->spec.enabled) state->execute(state->program, state->globals);
        state->callDepth = 0;
    } catch (...) { state->callDepth = 0; state->stopped = true; throw; }
}
bool Runtime::hasMethod(const std::string& name) const {
    return !impl_->state->stopped && impl_->state->spec.enabled && impl_->state->methods.count(name) != 0;
}
Value Runtime::call(const std::string& name, std::vector<Value> values) {
    impl_->state->stepLimit = stepLimit;
    Arguments args; args.modes.resize(values.size(), ArgumentMode::Value); args.values = std::move(values);
    return impl_->state->invoke(name, args);
}
Value Runtime::get(const std::string& name) const {
    auto& state = *impl_->state;
    return lower(name) == "this" ? state.root : state.resolve(name, state.symbols.slot(name), state.globals);
}
void Runtime::set(const std::string& name, Value value) {
    auto* target = impl_->state->globals.find(impl_->state->symbols.slot(name));
    if (!target) throw std::runtime_error("Undefined variable: " + name);
    *target = std::move(value);
}
void Runtime::stop() { impl_->state->stopped = true; }
const Spec& Runtime::spec() const { return impl_->state->spec; }

} // namespace eryscript
