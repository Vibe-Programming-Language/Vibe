#include "lexer/lexer.h"

#include <cctype>
#include <unordered_map>

namespace nova {

static const std::unordered_map<std::string_view, TokenType> kKeywords = {
    // basics
    {"var", TokenType::KwVar},
    {"let", TokenType::KwLet},
    {"const", TokenType::KwConst},
    {"fn", TokenType::KwFn},
    {"return", TokenType::KwReturn},
    {"if", TokenType::KwIf},
    {"else", TokenType::KwElse},
    {"for", TokenType::KwFor},
    {"in", TokenType::KwIn},
    {"while", TokenType::KwWhile},
    {"do", TokenType::KwDo},
    {"match", TokenType::KwMatch},
    {"break", TokenType::KwBreak},
    {"continue", TokenType::KwContinue},
    {"true", TokenType::KwTrue},
    {"false", TokenType::KwFalse},
    {"null", TokenType::KwNull},
    // OOP
    {"class", TokenType::KwClass},
    {"extends", TokenType::KwExtends},
    {"implements", TokenType::KwImplements},
    {"interface", TokenType::KwInterface},
    {"new", TokenType::KwNew},
    {"self", TokenType::KwSelf},
    {"super", TokenType::KwSuper},
    {"init", TokenType::KwInit},
    {"override", TokenType::KwOverride},
    {"private", TokenType::KwPrivate},
    {"public", TokenType::KwPublic},
    {"protected", TokenType::KwProtected},
    {"static", TokenType::KwStatic},
    {"get", TokenType::KwGet},
    {"set", TokenType::KwSet},
    // modules
    {"import", TokenType::KwImport},
    {"export", TokenType::KwExport},
    {"from", TokenType::KwFrom},
    {"as", TokenType::KwAs},
    // async
    {"async", TokenType::KwAsync},
    {"await", TokenType::KwAwait},
    {"spawn", TokenType::KwSpawn},
    // error handling
    {"try", TokenType::KwTry},
    {"catch", TokenType::KwCatch},
    {"finally", TokenType::KwFinally},
    {"throw", TokenType::KwThrow},
    // other
    {"enum", TokenType::KwEnum},
    {"type", TokenType::KwType},
    {"unsafe", TokenType::KwUnsafe},
};

std::string_view toString(TokenType t) {
  switch (t) {
  case TokenType::Eof: return "Eof";
  case TokenType::Error: return "Error";
  case TokenType::Identifier: return "Identifier";
  case TokenType::IntLiteral: return "IntLiteral";
  case TokenType::FloatLiteral: return "FloatLiteral";
  case TokenType::StringLiteral: return "StringLiteral";
  case TokenType::CharLiteral: return "CharLiteral";
  case TokenType::KwVar: return "var";
  case TokenType::KwLet: return "let";
  case TokenType::KwConst: return "const";
  case TokenType::KwFn: return "fn";
  case TokenType::KwReturn: return "return";
  case TokenType::KwIf: return "if";
  case TokenType::KwElse: return "else";
  case TokenType::KwFor: return "for";
  case TokenType::KwIn: return "in";
  case TokenType::KwWhile: return "while";
  case TokenType::KwDo: return "do";
  case TokenType::KwMatch: return "match";
  case TokenType::KwBreak: return "break";
  case TokenType::KwContinue: return "continue";
  case TokenType::KwTrue: return "true";
  case TokenType::KwFalse: return "false";
  case TokenType::KwNull: return "null";
  case TokenType::KwClass: return "class";
  case TokenType::KwExtends: return "extends";
  case TokenType::KwImplements: return "implements";
  case TokenType::KwInterface: return "interface";
  case TokenType::KwNew: return "new";
  case TokenType::KwSelf: return "self";
  case TokenType::KwSuper: return "super";
  case TokenType::KwInit: return "init";
  case TokenType::KwOverride: return "override";
  case TokenType::KwPrivate: return "private";
  case TokenType::KwPublic: return "public";
  case TokenType::KwProtected: return "protected";
  case TokenType::KwStatic: return "static";
  case TokenType::KwGet: return "get";
  case TokenType::KwSet: return "set";
  case TokenType::KwImport: return "import";
  case TokenType::KwExport: return "export";
  case TokenType::KwFrom: return "from";
  case TokenType::KwAs: return "as";
  case TokenType::KwAsync: return "async";
  case TokenType::KwAwait: return "await";
  case TokenType::KwSpawn: return "spawn";
  case TokenType::KwTry: return "try";
  case TokenType::KwCatch: return "catch";
  case TokenType::KwFinally: return "finally";
  case TokenType::KwThrow: return "throw";
  case TokenType::KwEnum: return "enum";
  case TokenType::KwType: return "type";
  case TokenType::KwUnsafe: return "unsafe";
  case TokenType::Plus: return "+";
  case TokenType::Minus: return "-";
  case TokenType::Star: return "*";
  case TokenType::Slash: return "/";
  case TokenType::Tilde: return "~";
  case TokenType::TildeSlash: return "~/";
  case TokenType::Percent: return "%";
  case TokenType::StarStar: return "**";
  case TokenType::Equal: return "=";
  case TokenType::EqualEqual: return "==";
  case TokenType::BangEqual: return "!=";
  case TokenType::Less: return "<";
  case TokenType::LessEqual: return "<=";
  case TokenType::Greater: return ">";
  case TokenType::GreaterEqual: return ">=";
  case TokenType::AmpAmp: return "&&";
  case TokenType::PipePipe: return "||";
  case TokenType::Bang: return "!";
  case TokenType::Amp: return "&";
  case TokenType::Pipe: return "|";
  case TokenType::Caret: return "^";
  case TokenType::LessLess: return "<<";
  case TokenType::GreaterGreater: return ">>";
  case TokenType::PlusEqual: return "+=";
  case TokenType::MinusEqual: return "-=";
  case TokenType::StarEqual: return "*=";
  case TokenType::SlashEqual: return "/=";
  case TokenType::PercentEqual: return "%=";
  case TokenType::Arrow: return "->";
  case TokenType::FatArrow: return "=>";
  case TokenType::Question: return "?";
  case TokenType::Colon: return ":";
  case TokenType::DotDot: return "..";
  case TokenType::DotDotDot: return "...";
  case TokenType::Dot: return ".";
  case TokenType::Comma: return ",";
  case TokenType::At: return "@";
  case TokenType::Hash: return "#";
  case TokenType::Semicolon: return ";";
  case TokenType::LParen: return "(";
  case TokenType::RParen: return ")";
  case TokenType::LBrace: return "{";
  case TokenType::RBrace: return "}";
  case TokenType::LBracket: return "[";
  case TokenType::RBracket: return "]";
  }
  return "?";
}

Lexer::Lexer(std::string filename, std::string source)
    : filename_(std::move(filename)), source_(std::move(source)) {}

char Lexer::cur() const { return atEnd() ? '\0' : source_[i_]; }
char Lexer::peekChar(int ahead) const {
  int j = i_ + ahead;
  return (j < 0 || j >= static_cast<int>(source_.size())) ? '\0' : source_[j];
}
bool Lexer::atEnd() const { return i_ >= static_cast<int>(source_.size()); }

char Lexer::advance() {
  char c = cur();
  if (!atEnd()) {
    i_++;
    if (c == '\n') {
      line_++;
      col_ = 1;
    } else {
      col_++;
    }
  }
  return c;
}

bool Lexer::match(char expected) {
  if (atEnd() || source_[i_] != expected)
    return false;
  advance();
  return true;
}

void Lexer::skipWhitespaceAndComments() {
  for (;;) {
    char c = cur();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
      continue;
    }
    // line comment: //
    if (c == '/' && peekChar(1) == '/') {
      while (cur() != '\n' && !atEnd())
        advance();
      continue;
    }
    // block comment: /* ... */
    if (c == '/' && peekChar(1) == '*') {
      advance();
      advance();
      while (!atEnd()) {
        if (cur() == '*' && peekChar(1) == '/') {
          advance();
          advance();
          break;
        }
        advance();
      }
      continue;
    }
    break;
  }
}

Token Lexer::make(TokenType t, int startOffset, int startLine, int startCol) {
  Token tok;
  tok.type = t;
  tok.lexeme = source_.substr(startOffset, i_ - startOffset);
  tok.pos.filename = filename_;
  tok.pos.offset = startOffset;
  tok.pos.line = startLine;
  tok.pos.column = startCol;
  return tok;
}

Token Lexer::errorToken(std::string message, int startOffset, int startLine,
                        int startCol) {
  Token tok;
  tok.type = TokenType::Error;
  tok.lexeme = std::move(message);
  tok.pos.filename = filename_;
  tok.pos.offset = startOffset;
  tok.pos.line = startLine;
  tok.pos.column = startCol;
  return tok;
}

Token Lexer::lexIdentifierOrKeyword(int startOffset, int startLine,
                                    int startCol) {
  while (std::isalnum(static_cast<unsigned char>(cur())) || cur() == '_')
    advance();
  auto text = std::string_view(source_).substr(startOffset, i_ - startOffset);
  auto it = kKeywords.find(text);
  if (it != kKeywords.end())
    return make(it->second, startOffset, startLine, startCol);
  return make(TokenType::Identifier, startOffset, startLine, startCol);
}

Token Lexer::lexNumber(int startOffset, int startLine, int startCol) {
  while (std::isdigit(static_cast<unsigned char>(cur())))
    advance();
  bool isFloat = false;
  if (cur() == '.' && std::isdigit(static_cast<unsigned char>(peekChar(1)))) {
    isFloat = true;
    advance();
    while (std::isdigit(static_cast<unsigned char>(cur())))
      advance();
  }
  // scientific notation: 1e10, 1.5e-3
  if (cur() == 'e' || cur() == 'E') {
    isFloat = true;
    advance();
    if (cur() == '+' || cur() == '-')
      advance();
    while (std::isdigit(static_cast<unsigned char>(cur())))
      advance();
  }
  return make(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral,
              startOffset, startLine, startCol);
}

Token Lexer::lexString(int startOffset, int startLine, int startCol) {
  while (!atEnd()) {
    char c = advance();
    if (c == '"')
      return make(TokenType::StringLiteral, startOffset, startLine, startCol);
    if (c == '\\' && !atEnd())
      advance();
    if (c == '\n')
      return errorToken("Unterminated string literal", startOffset, startLine,
                        startCol);
  }
  return errorToken("Unterminated string literal", startOffset, startLine,
                    startCol);
}

Token Lexer::lexChar(int startOffset, int startLine, int startCol) {
  if (atEnd())
    return errorToken("Unterminated char literal", startOffset, startLine,
                      startCol);
  char c = advance();
  if (c == '\\' && !atEnd())
    advance();
  if (!match('\''))
    return errorToken("Unterminated char literal", startOffset, startLine,
                      startCol);
  return make(TokenType::CharLiteral, startOffset, startLine, startCol);
}

Token Lexer::next() {
  if (peeked_) {
    Token t = *peeked_;
    peeked_.reset();
    return t;
  }

  skipWhitespaceAndComments();
  int startOffset = i_;
  int startLine = line_;
  int startCol = col_;

  if (atEnd()) {
    Token tok;
    tok.type = TokenType::Eof;
    tok.lexeme = "";
    tok.pos.filename = filename_;
    tok.pos.offset = i_;
    tok.pos.line = line_;
    tok.pos.column = col_;
    return tok;
  }

  char c = advance();

  // Identifiers / keywords
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
    return lexIdentifierOrKeyword(startOffset, startLine, startCol);

  // Numbers
  if (std::isdigit(static_cast<unsigned char>(c)))
    return lexNumber(startOffset, startLine, startCol);

  switch (c) {
  case '"': return lexString(startOffset, startLine, startCol);
  case '\'': return lexChar(startOffset, startLine, startCol);
  case ';': return make(TokenType::Semicolon, startOffset, startLine, startCol);
  case ',': return make(TokenType::Comma, startOffset, startLine, startCol);
  case '.':
    if (cur() == '.' && peekChar(1) == '.') { advance(); advance(); return make(TokenType::DotDotDot, startOffset, startLine, startCol); }
    if (cur() == '.') { advance(); return make(TokenType::DotDot, startOffset, startLine, startCol); }
    return make(TokenType::Dot, startOffset, startLine, startCol);
  case '(': return make(TokenType::LParen, startOffset, startLine, startCol);
  case ')': return make(TokenType::RParen, startOffset, startLine, startCol);
  case '{': return make(TokenType::LBrace, startOffset, startLine, startCol);
  case '}': return make(TokenType::RBrace, startOffset, startLine, startCol);
  case '[': return make(TokenType::LBracket, startOffset, startLine, startCol);
  case ']': return make(TokenType::RBracket, startOffset, startLine, startCol);
  case '@': return make(TokenType::At, startOffset, startLine, startCol);
  case '#': return make(TokenType::Hash, startOffset, startLine, startCol);
  case '+':
    if (match('=')) return make(TokenType::PlusEqual, startOffset, startLine, startCol);
    return make(TokenType::Plus, startOffset, startLine, startCol);
  case '-':
    if (match('>')) return make(TokenType::Arrow, startOffset, startLine, startCol);
    if (match('=')) return make(TokenType::MinusEqual, startOffset, startLine, startCol);
    return make(TokenType::Minus, startOffset, startLine, startCol);
  case '*':
    if (match('*')) return make(TokenType::StarStar, startOffset, startLine, startCol);
    if (match('=')) return make(TokenType::StarEqual, startOffset, startLine, startCol);
    return make(TokenType::Star, startOffset, startLine, startCol);
  case '/':
    // No // operator (it's always a comment). Integer division is ~/
    if (match('=')) return make(TokenType::SlashEqual, startOffset, startLine, startCol);
    return make(TokenType::Slash, startOffset, startLine, startCol);
  case '~':
    if (match('/')) return make(TokenType::TildeSlash, startOffset, startLine, startCol);
    return make(TokenType::Tilde, startOffset, startLine, startCol);
  case '%':
    if (match('=')) return make(TokenType::PercentEqual, startOffset, startLine, startCol);
    return make(TokenType::Percent, startOffset, startLine, startCol);
  case '=':
    if (match('=')) return make(TokenType::EqualEqual, startOffset, startLine, startCol);
    if (match('>')) return make(TokenType::FatArrow, startOffset, startLine, startCol);
    return make(TokenType::Equal, startOffset, startLine, startCol);
  case '!':
    if (match('=')) return make(TokenType::BangEqual, startOffset, startLine, startCol);
    return make(TokenType::Bang, startOffset, startLine, startCol);
  case '<':
    if (match('<')) return make(TokenType::LessLess, startOffset, startLine, startCol);
    if (match('=')) return make(TokenType::LessEqual, startOffset, startLine, startCol);
    return make(TokenType::Less, startOffset, startLine, startCol);
  case '>':
    if (match('>')) return make(TokenType::GreaterGreater, startOffset, startLine, startCol);
    if (match('=')) return make(TokenType::GreaterEqual, startOffset, startLine, startCol);
    return make(TokenType::Greater, startOffset, startLine, startCol);
  case '&':
    if (match('&')) return make(TokenType::AmpAmp, startOffset, startLine, startCol);
    return make(TokenType::Amp, startOffset, startLine, startCol);
  case '|':
    if (match('|')) return make(TokenType::PipePipe, startOffset, startLine, startCol);
    return make(TokenType::Pipe, startOffset, startLine, startCol);
  case '^': return make(TokenType::Caret, startOffset, startLine, startCol);
  case '?': return make(TokenType::Question, startOffset, startLine, startCol);
  case ':': return make(TokenType::Colon, startOffset, startLine, startCol);
  default: break;
  }

  return errorToken(std::string("Unexpected character: '") + c + "'",
                    startOffset, startLine, startCol);
}

Token Lexer::peek() {
  if (!peeked_)
    peeked_ = next();
  return *peeked_;
}

} // namespace nova
