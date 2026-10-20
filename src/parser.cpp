#include "parser.h"

#include <cctype>
#include <utility>

namespace mediagrep {

namespace {

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

struct Token {
    enum class Type { End, LParen, RParen, Op, String, Word };
    Type type = Type::End;
    std::size_t offset = 0;
    Op op = Op::Eq;     // Type::Op
    std::string text;   // String: unescaped contents; Word: the word
};

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isOpStart(char c) { return c == '=' || c == '!' || c == '~' || c == '<' || c == '>'; }
bool isQuote(char c) { return c == '"' || c == '\''; }
bool isWordChar(char c) {
    return !isSpace(c) && !isQuote(c) && !isOpStart(c) && c != '(' && c != ')';
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {}

    Token next() {
        while (pos_ < text_.size() && isSpace(text_[pos_])) ++pos_;
        Token tok;
        tok.offset = pos_;
        if (pos_ >= text_.size()) return tok;

        const char c = text_[pos_];
        if (c == '(') { ++pos_; tok.type = Token::Type::LParen; return tok; }
        if (c == ')') { ++pos_; tok.type = Token::Type::RParen; return tok; }
        if (isQuote(c)) return lexString(tok);
        if (isOpStart(c)) return lexOp(tok);

        tok.type = Token::Type::Word;
        while (pos_ < text_.size() && isWordChar(text_[pos_])) tok.text += text_[pos_++];
        return tok;
    }

private:
    Token lexString(Token tok) {
        const char quote = text_[pos_++];
        tok.type = Token::Type::String;
        while (true) {
            if (pos_ >= text_.size())
                throw ParseError(tok.offset, "unterminated string");
            const char c = text_[pos_++];
            if (c == quote) return tok;
            if (c == '\\' && pos_ < text_.size()) {
                tok.text += text_[pos_++];
                continue;
            }
            tok.text += c;
        }
    }

    Token lexOp(Token tok) {
        tok.type = Token::Type::Op;
        const char c = text_[pos_];
        const char n = pos_ + 1 < text_.size() ? text_[pos_ + 1] : '\0';
        auto two = [&](Op op) { pos_ += 2; tok.op = op; return tok; };
        auto one = [&](Op op) { pos_ += 1; tok.op = op; return tok; };
        switch (c) {
            case '=': return one(Op::Eq);
            case '~': return one(Op::Match);
            case '<': return n == '=' ? two(Op::Le) : one(Op::Lt);
            case '>': return n == '=' ? two(Op::Ge) : one(Op::Gt);
            case '!':
                if (n == '=') return two(Op::Ne);
                if (n == '~') return two(Op::NotMatch);
                throw ParseError(tok.offset, "expected '!=' or '!~'");
        }
        throw ParseError(tok.offset, "unexpected character");
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    explicit Parser(std::string_view text) : lexer_(text) { advance(); }

    ExprPtr parseAll() {
        ExprPtr e = parseOr();
        if (tok_.type != Token::Type::End)
            throw ParseError(tok_.offset, "unexpected input after expression");
        return e;
    }

private:
    void advance() { tok_ = lexer_.next(); }

    bool atKeyword(std::string_view kw) const {
        return tok_.type == Token::Type::Word && iequals(tok_.text, kw);
    }

    bool atAnyKeyword() const {
        return atKeyword("and") || atKeyword("or") || atKeyword("not");
    }

    ExprPtr parseOr() { return parseChain(Expr::Kind::Or, "or", &Parser::parseAnd); }
    ExprPtr parseAnd() { return parseChain(Expr::Kind::And, "and", &Parser::parseNot); }

    ExprPtr parseChain(Expr::Kind kind, std::string_view keyword,
                       ExprPtr (Parser::*sub)()) {
        ExprPtr first = (this->*sub)();
        if (!atKeyword(keyword)) return first;
        auto node = std::make_unique<Expr>();
        node->kind = kind;
        node->children.push_back(std::move(first));
        while (atKeyword(keyword)) {
            advance();
            node->children.push_back((this->*sub)());
        }
        return node;
    }

    ExprPtr parseNot() {
        if (!atKeyword("not")) return parseAtom();
        advance();
        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::Not;
        node->children.push_back(parseNot());
        return node;
    }

    ExprPtr parseAtom() {
        switch (tok_.type) {
            case Token::Type::LParen: {
                advance();
                ExprPtr inner = parseOr();
                if (tok_.type != Token::Type::RParen)
                    throw ParseError(tok_.offset, "expected ')'");
                advance();
                return inner;
            }
            case Token::Type::String: {
                auto node = std::make_unique<Expr>();
                node->kind = Expr::Kind::Pattern;
                node->value = tok_.text;
                advance();
                return node;
            }
            case Token::Type::Word:
                if (atAnyKeyword())
                    throw ParseError(tok_.offset, "expected expression");
                return parseFieldAtom();
            case Token::Type::Op:
                throw ParseError(tok_.offset, "expected field or pattern before operator");
            case Token::Type::RParen:
                throw ParseError(tok_.offset, "unexpected ')'");
            case Token::Type::End:
                throw ParseError(tok_.offset, "expected expression");
        }
        throw ParseError(tok_.offset, "expected expression");
    }

    ExprPtr parseFieldAtom() {
        auto node = std::make_unique<Expr>();
        node->field = parseField(tok_.text, tok_.offset);
        advance();
        if (tok_.type != Token::Type::Op) {
            node->kind = Expr::Kind::Exists;
            return node;
        }
        node->kind = Expr::Kind::Compare;
        node->op = tok_.op;
        const std::string opText(opName(tok_.op));
        advance();
        if (tok_.type != Token::Type::String && tok_.type != Token::Type::Word)
            throw ParseError(tok_.offset, "expected value after '" + opText + "'");
        node->value = tok_.text;
        advance();
        return node;
    }

    // word: "Title", "Audio/Language", "Audio[2]/Language", "Duration/String"
    static FieldRef parseField(const std::string& word, std::size_t offset) {
        FieldRef ref;
        std::size_t i = 0;
        while (i < word.size() && word[i] != '/' && word[i] != '[') ++i;
        const std::optional<TrackKind> kind = trackKindFromName(word.substr(0, i));
        if (!kind || i == word.size()) {
            ref.name = word;  // no track prefix; slashes belong to the name
            return ref;
        }
        ref.track = *kind;
        if (word[i] == '[') {
            ++i;
            const std::size_t digitsAt = i;
            std::size_t index = 0;
            while (i < word.size() && std::isdigit(static_cast<unsigned char>(word[i])))
                index = index * 10 + static_cast<std::size_t>(word[i++] - '0');
            if (i == digitsAt || index == 0)
                throw ParseError(offset + digitsAt, "track index must be a number >= 1");
            if (i >= word.size() || word[i] != ']')
                throw ParseError(offset + i, "expected ']'");
            ++i;
            ref.index = index;
        }
        if (i >= word.size() || word[i] != '/')
            throw ParseError(offset + i, "expected '/' and a field name after track");
        ref.name = word.substr(i + 1);
        if (ref.name.empty())
            throw ParseError(offset + i + 1, "expected field name after '/'");
        return ref;
    }

    Lexer lexer_;
    Token tok_;
};

}  // namespace

ExprPtr parse(std::string_view text) {
    return Parser(text).parseAll();
}

}  // namespace mediagrep
