#pragma once

#include "lexer/tokens.h"

#include <string>
#include <string_view>
#include <vector>

namespace nova {

class Lexer {
public:
  Lexer(std::string filename, std::string source);

  const std::string& filename() const { return filename_; }
  const std::string& source() const { return source_; }

  Token next();
  Token peek();

private:
  char cur() const;
  char peekChar(int ahead) const;
  bool atEnd() const;
  char advance();
  bool match(char expected);

  void skipWhitespaceAndComments();

  Token make(TokenType t, int startOffset, int startLine, int startCol);
  Token errorToken(std::string message, int startOffset, int startLine, int startCol);

  Token lexIdentifierOrKeyword(int startOffset, int startLine, int startCol);
  Token lexNumber(int startOffset, int startLine, int startCol);
  Token lexString(int startOffset, int startLine, int startCol);
  Token lexChar(int startOffset, int startLine, int startCol);

  std::string filename_;
  std::string source_;
  int i_ = 0;
  int line_ = 1;
  int col_ = 1;

  std::optional<Token> peeked_;
};

} // namespace nova
