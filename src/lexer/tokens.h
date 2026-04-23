#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nova {

enum class TokenType {
  // Special
  Eof,
  Error,

  // Identifiers + literals
  Identifier,
  IntLiteral,
  FloatLiteral,
  StringLiteral,
  CharLiteral,

  // ── Keywords ──────────────────────────────────
  KwVar,
  KwLet,
  KwConst,
  KwFn,
  KwReturn,
  KwIf,
  KwElse,
  KwFor,
  KwIn,
  KwWhile,
  KwDo,
  KwMatch,
  KwBreak,
  KwContinue,
  KwTrue,
  KwFalse,
  KwNull,

  // OOP
  KwClass,
  KwExtends,
  KwImplements,
  KwInterface,
  KwNew,
  KwSelf,
  KwSuper,
  KwInit,
  KwOverride,
  KwPrivate,
  KwPublic,
  KwProtected,
  KwStatic,
  KwGet,
  KwSet,

  // Modules
  KwImport,
  KwExport,
  KwFrom,
  KwAs,

  // Python Interop
  KwPython,
  KwPyImport,

  // Async / concurrency
  KwAsync,
  KwAwait,
  KwSpawn,

  // Error handling
  KwTry,
  KwCatch,
  KwFinally,
  KwThrow,

  // Other
  KwEnum,
  KwType,
  KwUnsafe,

  // ── Operators ─────────────────────────────────
  Plus,
  Minus,
  Star,
  Slash,
  Tilde,
  TildeSlash,   // ~/  integer division
  Percent,
  StarStar,     // **

  Equal,
  EqualEqual,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,

  AmpAmp,
  PipePipe,
  Bang,

  Amp,
  Pipe,
  Caret,
  LessLess,        // <<
  GreaterGreater,  // >>

  PlusEqual,
  MinusEqual,
  StarEqual,
  SlashEqual,
  PercentEqual,

  Arrow,       // ->
  FatArrow,    // =>
  Question,    // ?
  Colon,       // :
  DotDot,      // ..
  DotDotDot,   // ...
  Dot,         // .
  Comma,
  At,          // @
  Hash,        // #

  // ── Punctuation ───────────────────────────────
  Semicolon,
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
};

struct SourcePos {
  std::string filename;
  int line = 1;
  int column = 1;
  int offset = 0;
};

struct Token {
  TokenType type = TokenType::Error;
  std::string lexeme;
  SourcePos pos{};
};

std::string_view toString(TokenType t);

} // namespace nova

