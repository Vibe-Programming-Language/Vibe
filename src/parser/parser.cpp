#include "parser/parser.h"

#include <utility>

namespace nova {

Parser::Parser(Lexer lexer) : lexer_(std::move(lexer)) {}

Token Parser::peek() {
  if (!hasCurrent_) {
    current_ = lexer_.next();
    hasCurrent_ = true;
  }
  return current_;
}

Token Parser::next() {
  Token t = peek();
  hasCurrent_ = false;
  return t;
}

bool Parser::check(TokenType t) { return peek().type == t; }

bool Parser::match(TokenType t) {
  if (check(t)) {
    next();
    return true;
  }
  return false;
}

Token Parser::expect(TokenType t, const std::string& message) {
  Token got = peek();
  if (got.type != t)
    throw ParseError(got, message);
  return next();
}

ExprPtr Parser::makeExpr(SourcePos pos, Expr e) {
  auto p = std::make_unique<Expr>();
  p->pos = std::move(pos);
  p->node = std::move(e.node);
  return p;
}

StmtPtr Parser::makeStmt(SourcePos pos, Stmt s) {
  auto p = std::make_unique<Stmt>();
  p->pos = std::move(pos);
  p->node = std::move(s.node);
  return p;
}

// ═══════════════════════════════════════════════════
//  Program
// ═══════════════════════════════════════════════════

Program Parser::parseProgram() {
  Program prog;
  while (!check(TokenType::Eof)) {
    prog.stmts.push_back(parseDeclOrStmt());
  }
  return prog;
}

// ═══════════════════════════════════════════════════
//  Top-level declarations and statements
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseDeclOrStmt() {
  if (match(TokenType::KwVar) || match(TokenType::KwLet))
    return parseVarOrConst(false);
  if (match(TokenType::KwConst))
    return parseVarOrConst(true);
  if (check(TokenType::KwFn))
    return parseFnDecl();
  if (check(TokenType::KwAsync)) {
    Token async = next();
    if (check(TokenType::KwFn))
      return parseFnDecl(true);
    throw ParseError(peek(), "Expected 'fn' after 'async'");
  }
  if (check(TokenType::KwClass))
    return parseClass();
  if (check(TokenType::KwInterface))
    return parseInterface();
  if (check(TokenType::KwEnum))
    return parseEnum();
  if (check(TokenType::KwImport))
    return parseImport();
  if (check(TokenType::KwExport))
    return parseExport();
  return parseStatement();
}

StmtPtr Parser::parseStatement() {
  Token t = peek();
  switch (t.type) {
  case TokenType::LBrace: return parseBlock();
  case TokenType::KwIf: return parseIf();
  case TokenType::KwWhile: return parseWhile();
  case TokenType::KwDo: return parseDoWhile();
  case TokenType::KwFor: return parseFor();
  case TokenType::KwMatch: return parseMatch();
  case TokenType::KwReturn: return parseReturn();
  case TokenType::KwBreak: return parseBreak();
  case TokenType::KwContinue: return parseContinue();
  case TokenType::KwThrow: return parseThrow();
  case TokenType::KwTry: return parseTryCatch();
  case TokenType::KwSpawn: return parseSpawn();
  case TokenType::KwUnsafe: return parseUnsafe();
  default: return parseExprStmt();
  }
}

// ═══════════════════════════════════════════════════
//  Block
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseBlock() {
  Token lb = expect(TokenType::LBrace, "Expected '{' to start block");
  BlockStmt blk;
  while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
    blk.stmts.push_back(parseDeclOrStmt());
  }
  expect(TokenType::RBrace, "Expected '}' to end block");
  Stmt s;
  s.pos = lb.pos;
  s.node = std::move(blk);
  return makeStmt(lb.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Type annotations
// ═══════════════════════════════════════════════════

std::optional<TypeAnn> Parser::parseOptionalTypeAnn() {
  if (!match(TokenType::Colon))
    return std::nullopt;
  return parseTypeAnn();
}

TypeAnn Parser::parseTypeAnn() {
  Token name = peek();
  if (name.type != TokenType::Identifier &&
      name.type != TokenType::KwFn) {
    // Allow type keywords used as identifiers
    throw ParseError(name, "Expected type name");
  }
  next();
  std::string text = name.lexeme;

  // Handle generic: List<int>, Map<str, int>
  if (check(TokenType::Less)) {
    text += "<";
    next();
    int depth = 1;
    while (depth > 0 && !check(TokenType::Eof)) {
      Token t = next();
      if (t.type == TokenType::Less) depth++;
      if (t.type == TokenType::Greater) {
        depth--;
        if (depth == 0) { text += ">"; break; }
      }
      text += t.lexeme;
      if (depth > 0 && t.type != TokenType::Greater)
        if (check(TokenType::Comma)) { text += ", "; next(); }
    }
  }
  // Handle array: int[]
  if (check(TokenType::LBracket)) {
    next();
    expect(TokenType::RBracket, "Expected ']' in array type");
    text += "[]";
  }
  // Handle optional: int?
  if (check(TokenType::Question)) {
    next();
    text += "?";
  }
  return TypeAnn{text};
}

// ═══════════════════════════════════════════════════
//  Variable / Constant
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseVarOrConst(bool isConst) {
  Token name = expect(TokenType::Identifier, "Expected variable name");
  auto type = parseOptionalTypeAnn();

  ExprPtr init;
  if (match(TokenType::Equal)) {
    init = parseExpression();
  } else if (isConst) {
    throw ParseError(peek(), "const requires an initializer");
  }
  expect(TokenType::Semicolon, "Expected ';' after variable declaration");

  VarDeclStmt vd;
  vd.isConst = isConst;
  vd.name = name;
  vd.type = std::move(type);
  vd.init = std::move(init);
  Stmt s;
  s.pos = name.pos;
  s.node = std::move(vd);
  return makeStmt(name.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Function declaration
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseFnDecl(bool isAsync, bool isStatic, bool isOverride) {
  expect(TokenType::KwFn, "Expected 'fn'");
  Token name = expect(TokenType::Identifier, "Expected function name");
  expect(TokenType::LParen, "Expected '(' after function name");

  std::vector<Param> params;
  if (!check(TokenType::RParen)) {
    for (;;) {
      Token pName = expect(TokenType::Identifier, "Expected parameter name");
      auto pType = parseOptionalTypeAnn();
      std::optional<ExprPtr> def;
      if (match(TokenType::Equal)) {
        def = parseExpression();
      }
      params.push_back(Param{pName, std::move(pType), std::move(def)});
      if (!match(TokenType::Comma))
        break;
    }
  }
  expect(TokenType::RParen, "Expected ')' after parameters");

  std::optional<TypeAnn> ret;
  if (match(TokenType::Arrow)) {
    ret = parseTypeAnn();
  }

  StmtPtr body = parseBlock();

  FnDeclStmt fn;
  fn.isAsync = isAsync;
  fn.isStatic = isStatic;
  fn.isOverride = isOverride;
  fn.name = name;
  fn.params = std::move(params);
  fn.retType = std::move(ret);
  fn.body = std::move(body);

  Stmt s;
  s.pos = name.pos;
  s.node = std::move(fn);
  return makeStmt(name.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  If / Else
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseIf() {
  Token kw = expect(TokenType::KwIf, "Expected 'if'");
  expect(TokenType::LParen, "Expected '(' after if");
  ExprPtr cond = parseExpression();
  expect(TokenType::RParen, "Expected ')' after if condition");
  StmtPtr thenBranch = parseStatement();
  std::optional<StmtPtr> elseBranch;
  if (match(TokenType::KwElse)) {
    elseBranch = parseStatement();
  }
  IfStmt ifs;
  ifs.cond = std::move(cond);
  ifs.thenBranch = std::move(thenBranch);
  ifs.elseBranch = std::move(elseBranch);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(ifs);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  While
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseWhile() {
  Token kw = expect(TokenType::KwWhile, "Expected 'while'");
  expect(TokenType::LParen, "Expected '(' after while");
  ExprPtr cond = parseExpression();
  expect(TokenType::RParen, "Expected ')' after while condition");
  StmtPtr body = parseStatement();
  WhileStmt ws;
  ws.cond = std::move(cond);
  ws.body = std::move(body);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(ws);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Do-While
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseDoWhile() {
  Token kw = expect(TokenType::KwDo, "Expected 'do'");
  StmtPtr body = parseBlock();
  expect(TokenType::KwWhile, "Expected 'while' after do block");
  expect(TokenType::LParen, "Expected '(' after while");
  ExprPtr cond = parseExpression();
  expect(TokenType::RParen, "Expected ')' after condition");
  expect(TokenType::Semicolon, "Expected ';' after do-while");
  DoWhileStmt dw;
  dw.body = std::move(body);
  dw.cond = std::move(cond);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(dw);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  For / For-in
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseFor() {
  Token kw = expect(TokenType::KwFor, "Expected 'for'");

  // Optional parentheses
  bool hasParen = match(TokenType::LParen);

  // for-in: for name in expr { } or for (name in expr) { }
  if (check(TokenType::KwVar) || check(TokenType::Identifier)) {
    bool hadVar = false;
    if (match(TokenType::KwVar))
      hadVar = true;
    if (check(TokenType::Identifier)) {
      Token name = next();
      if (match(TokenType::KwIn)) {
        ExprPtr it = parseExpression();
        if (hasParen) expect(TokenType::RParen, "Expected ')' after for-in");
        StmtPtr body = parseStatement();
        (void)hadVar;
        ForInStmt fis;
        fis.name = name;
        fis.iterable = std::move(it);
        fis.body = std::move(body);
        Stmt s;
        s.pos = kw.pos;
        s.node = std::move(fis);
        return makeStmt(kw.pos, std::move(s));
      }
      // C-style for with var: for (var i = 0; ...)
      if (hadVar) {
        std::optional<TypeAnn> ty = parseOptionalTypeAnn();
        ExprPtr initExpr;
        if (match(TokenType::Equal)) {
          initExpr = parseExpression();
        }
        expect(TokenType::Semicolon, "Expected ';' after for-init");
        VarDeclStmt vd;
        vd.isConst = false;
        vd.name = name;
        vd.type = std::move(ty);
        vd.init = std::move(initExpr);
        Stmt st;
        st.pos = name.pos;
        st.node = std::move(vd);
        std::optional<StmtPtr> init = makeStmt(st.pos, std::move(st));

        std::optional<ExprPtr> cond;
        if (!check(TokenType::Semicolon))
          cond = parseExpression();
        expect(TokenType::Semicolon, "Expected ';' after for condition");

        std::optional<ExprPtr> post;
        if (hasParen) {
          if (!check(TokenType::RParen))
            post = parseExpression();
          expect(TokenType::RParen, "Expected ')' after for clauses");
        } else {
          if (!check(TokenType::LBrace))
            post = parseExpression();
        }

        StmtPtr body = parseStatement();
        ForStmt fs;
        fs.init = std::move(init);
        fs.cond = std::move(cond);
        fs.post = std::move(post);
        fs.body = std::move(body);
        Stmt s;
        s.pos = kw.pos;
        s.node = std::move(fs);
        return makeStmt(kw.pos, std::move(s));
      }

      // No var: reconstruct identifier expression and parse as C-style for
      IdentExpr id{name};
      Expr ident;
      ident.pos = name.pos;
      ident.node = std::move(id);
      ExprPtr identExpr = makeExpr(name.pos, std::move(ident));

      // Check if assignment follows
      Token t2 = peek();
      if (t2.type == TokenType::Equal || t2.type == TokenType::PlusEqual ||
          t2.type == TokenType::MinusEqual || t2.type == TokenType::StarEqual ||
          t2.type == TokenType::SlashEqual || t2.type == TokenType::PercentEqual) {
        Token op = next();
        ExprPtr value = parseExpression();
        AssignExpr asn{std::move(identExpr), op, std::move(value)};
        Expr assignNode;
        assignNode.pos = name.pos;
        assignNode.node = std::move(asn);
        ExprPtr assignExpr = makeExpr(name.pos, std::move(assignNode));

        expect(TokenType::Semicolon, "Expected ';' after for-init");

        ExprStmt es;
        es.expr = std::move(assignExpr);
        Stmt initSt;
        initSt.pos = name.pos;
        initSt.node = std::move(es);
        std::optional<StmtPtr> init = makeStmt(initSt.pos, std::move(initSt));

        std::optional<ExprPtr> cond;
        if (!check(TokenType::Semicolon))
          cond = parseExpression();
        expect(TokenType::Semicolon, "Expected ';' after for condition");

        std::optional<ExprPtr> post;
        if (hasParen) {
          if (!check(TokenType::RParen))
            post = parseExpression();
          expect(TokenType::RParen, "Expected ')' after for clauses");
        } else {
          if (!check(TokenType::LBrace))
            post = parseExpression();
        }

        StmtPtr body = parseStatement();
        ForStmt fs;
        fs.init = std::move(init);
        fs.cond = std::move(cond);
        fs.post = std::move(post);
        fs.body = std::move(body);
        Stmt s;
        s.pos = kw.pos;
        s.node = std::move(fs);
        return makeStmt(kw.pos, std::move(s));
      }

      // Fall through - treat as expression statement init
      throw ParseError(t2, "Expected 'in', '=', or ';' after identifier in for(...)");
    } else if (hadVar) {
      throw ParseError(peek(), "Expected variable name after 'var'");
    }
  }

  // C-style for: for (; cond; post) body
  std::optional<StmtPtr> init;
  if (!check(TokenType::Semicolon)) {
    if (match(TokenType::KwVar)) {
      init = parseVarOrConst(false);
    } else {
      ExprPtr e = parseExpression();
      expect(TokenType::Semicolon, "Expected ';' after for-init expression");
      ExprStmt es;
      es.expr = std::move(e);
      Stmt st;
      st.pos = kw.pos;
      st.node = std::move(es);
      init = makeStmt(kw.pos, std::move(st));
    }
  } else {
    expect(TokenType::Semicolon, "Expected ';'");
  }

  std::optional<ExprPtr> cond;
  if (!check(TokenType::Semicolon))
    cond = parseExpression();
  expect(TokenType::Semicolon, "Expected ';' after for condition");

  std::optional<ExprPtr> post;
  if (hasParen) {
    if (!check(TokenType::RParen))
      post = parseExpression();
    expect(TokenType::RParen, "Expected ')' after for clauses");
  } else {
    if (!check(TokenType::LBrace))
      post = parseExpression();
  }

  StmtPtr body = parseStatement();
  ForStmt fs;
  fs.init = std::move(init);
  fs.cond = std::move(cond);
  fs.post = std::move(post);
  fs.body = std::move(body);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(fs);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Match
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseMatch() {
  Token kw = expect(TokenType::KwMatch, "Expected 'match'");
  // Optional parentheses: match val { } or match (val) { }
  bool hasParen = match(TokenType::LParen);
  ExprPtr target = parseExpression();
  if (hasParen) expect(TokenType::RParen, "Expected ')' after match target");
  expect(TokenType::LBrace, "Expected '{' to start match");

  std::vector<MatchArm> arms;
  while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
    std::optional<ExprPtr> pat;
    if (check(TokenType::Identifier) && peek().lexeme == "_") {
      (void)next();
      pat = std::nullopt;
    } else {
      pat = parseExpression();
    }
    // Accept both => and -> for match arms
    if (!match(TokenType::FatArrow) && !match(TokenType::Arrow))
      throw ParseError(peek(), "Expected '=>' in match arm");

    StmtPtr action;
    if (check(TokenType::LBrace)) {
      action = parseBlock();
    } else {
      ExprPtr e = parseExpression();
      if (check(TokenType::Semicolon))
        next();
      ExprStmt es;
      es.expr = std::move(e);
      Stmt st;
      st.pos = kw.pos;
      st.node = std::move(es);
      action = makeStmt(st.pos, std::move(st));
    }
    arms.push_back(MatchArm{std::move(pat), std::move(action)});
  }
  expect(TokenType::RBrace, "Expected '}' to end match");

  MatchStmt ms;
  ms.target = std::move(target);
  ms.arms = std::move(arms);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(ms);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Return / Break / Continue / Throw
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseReturn() {
  Token kw = expect(TokenType::KwReturn, "Expected 'return'");
  std::optional<ExprPtr> value;
  if (!check(TokenType::Semicolon))
    value = parseExpression();
  expect(TokenType::Semicolon, "Expected ';' after return");
  ReturnStmt rs;
  rs.kw = kw;
  rs.value = std::move(value);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(rs);
  return makeStmt(kw.pos, std::move(s));
}

StmtPtr Parser::parseBreak() {
  Token kw = expect(TokenType::KwBreak, "Expected 'break'");
  expect(TokenType::Semicolon, "Expected ';' after break");
  BreakStmt bs{kw};
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(bs);
  return makeStmt(kw.pos, std::move(s));
}

StmtPtr Parser::parseContinue() {
  Token kw = expect(TokenType::KwContinue, "Expected 'continue'");
  expect(TokenType::Semicolon, "Expected ';' after continue");
  ContinueStmt cs{kw};
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(cs);
  return makeStmt(kw.pos, std::move(s));
}

StmtPtr Parser::parseThrow() {
  Token kw = expect(TokenType::KwThrow, "Expected 'throw'");
  ExprPtr value = parseExpression();
  expect(TokenType::Semicolon, "Expected ';' after throw");
  ThrowStmt ts;
  ts.kw = kw;
  ts.value = std::move(value);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(ts);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Try / Catch / Finally
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseTryCatch() {
  Token kw = expect(TokenType::KwTry, "Expected 'try'");
  StmtPtr tryBody = parseBlock();

  std::vector<CatchClause> catches;
  while (match(TokenType::KwCatch)) {
    CatchClause cc;
    expect(TokenType::LParen, "Expected '(' after catch");
    if (check(TokenType::Identifier)) {
      Token first = next();
      if (match(TokenType::KwAs)) {
        // catch (ErrorType as varName)
        cc.typeName = first;
        cc.varName = expect(TokenType::Identifier, "Expected variable name after 'as'");
      } else {
        // catch (varName) — bind error to variable
        cc.varName = first;
      }
    }
    expect(TokenType::RParen, "Expected ')' after catch clause");
    cc.body = parseBlock();
    catches.push_back(std::move(cc));
  }

  std::optional<StmtPtr> finallyBody;
  if (match(TokenType::KwFinally)) {
    finallyBody = parseBlock();
  }

  if (catches.empty() && !finallyBody) {
    throw ParseError(kw, "try requires at least one catch or finally clause");
  }

  TryCatchStmt tc;
  tc.tryBody = std::move(tryBody);
  tc.catches = std::move(catches);
  tc.finallyBody = std::move(finallyBody);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(tc);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Class
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseClass() {
  Token kw = expect(TokenType::KwClass, "Expected 'class'");
  Token name = expect(TokenType::Identifier, "Expected class name");

  std::optional<Token> superClass;
  if (match(TokenType::KwExtends)) {
    superClass = expect(TokenType::Identifier, "Expected superclass name");
  }

  std::vector<Token> interfaces;
  if (match(TokenType::KwImplements)) {
    for (;;) {
      interfaces.push_back(expect(TokenType::Identifier, "Expected interface name"));
      if (!match(TokenType::Comma))
        break;
    }
  }

  expect(TokenType::LBrace, "Expected '{' to start class body");

  ClassDeclStmt cls;
  cls.name = name;
  cls.superClass = superClass;
  cls.interfaces = interfaces;

  while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
    ClassMember::Access access = ClassMember::Access::Public;
    bool isStatic = false;
    bool isOverride = false;

    // Parse access modifiers
    if (match(TokenType::KwPrivate)) access = ClassMember::Access::Private;
    else if (match(TokenType::KwPublic)) access = ClassMember::Access::Public;
    else if (match(TokenType::KwProtected)) access = ClassMember::Access::Protected;

    if (match(TokenType::KwStatic)) isStatic = true;
    if (match(TokenType::KwOverride)) isOverride = true;

    // Constructor
    if (check(TokenType::KwInit)) {
      next();
      expect(TokenType::LParen, "Expected '(' after init");
      std::vector<Param> params;
      if (!check(TokenType::RParen)) {
        for (;;) {
          Token pName = expect(TokenType::Identifier, "Expected parameter name");
          auto pType = parseOptionalTypeAnn();
          std::optional<ExprPtr> def;
          if (match(TokenType::Equal))
            def = parseExpression();
          params.push_back(Param{pName, std::move(pType), std::move(def)});
          if (!match(TokenType::Comma))
            break;
        }
      }
      expect(TokenType::RParen, "Expected ')' after init parameters");
      StmtPtr body = parseBlock();
      cls.initDecl = ClassInitDecl{std::move(params), std::move(body)};
      continue;
    }

    // Method
    if (check(TokenType::KwFn) || check(TokenType::KwAsync)) {
      bool isAsync = false;
      if (match(TokenType::KwAsync)) isAsync = true;
      expect(TokenType::KwFn, "Expected 'fn'");
      Token mName = expect(TokenType::Identifier, "Expected method name");
      expect(TokenType::LParen, "Expected '(' after method name");

      std::vector<Param> params;
      if (!check(TokenType::RParen)) {
        for (;;) {
          Token pName = expect(TokenType::Identifier, "Expected parameter name");
          auto pType = parseOptionalTypeAnn();
          std::optional<ExprPtr> def;
          if (match(TokenType::Equal))
            def = parseExpression();
          params.push_back(Param{pName, std::move(pType), std::move(def)});
          if (!match(TokenType::Comma))
            break;
        }
      }
      expect(TokenType::RParen, "Expected ')' after parameters");

      std::optional<TypeAnn> ret;
      if (match(TokenType::Arrow))
        ret = parseTypeAnn();

      StmtPtr body = parseBlock();
      FnDeclStmt fn;
      fn.isAsync = isAsync;
      fn.isStatic = isStatic;
      fn.isOverride = isOverride;
      fn.name = mName;
      fn.params = std::move(params);
      fn.retType = std::move(ret);
      fn.body = std::move(body);
      ClassMethodDecl md;
      md.access = access;
      md.method = std::move(fn);
      cls.methods.push_back(std::move(md));
      continue;
    }

    // Field: var name : type = init;
    if (match(TokenType::KwVar)) {
      Token fname = expect(TokenType::Identifier, "Expected field name");
      auto ftype = parseOptionalTypeAnn();
      ExprPtr finit;
      if (match(TokenType::Equal))
        finit = parseExpression();
      expect(TokenType::Semicolon, "Expected ';' after field declaration");
      ClassFieldDecl fd;
      fd.access = access;
      fd.isStatic = isStatic;
      fd.name = fname;
      fd.type = std::move(ftype);
      fd.init = std::move(finit);
      cls.fields.push_back(std::move(fd));
      continue;
    }

    throw ParseError(peek(), "Expected field, method, or init inside class body");
  }

  expect(TokenType::RBrace, "Expected '}' to end class body");

  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(cls);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Interface
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseInterface() {
  Token kw = expect(TokenType::KwInterface, "Expected 'interface'");
  Token name = expect(TokenType::Identifier, "Expected interface name");
  expect(TokenType::LBrace, "Expected '{' to start interface body");

  InterfaceDeclStmt iface;
  iface.name = name;

  while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
    expect(TokenType::KwFn, "Expected 'fn' in interface body");
    Token mName = expect(TokenType::Identifier, "Expected method name");
    expect(TokenType::LParen, "Expected '(' after method name");

    std::vector<Param> params;
    if (!check(TokenType::RParen)) {
      for (;;) {
        Token pName = expect(TokenType::Identifier, "Expected parameter name");
        auto pType = parseOptionalTypeAnn();
        params.push_back(Param{pName, std::move(pType), std::nullopt});
        if (!match(TokenType::Comma))
          break;
      }
    }
    expect(TokenType::RParen, "Expected ')'");

    std::optional<TypeAnn> ret;
    if (match(TokenType::Arrow))
      ret = parseTypeAnn();
    expect(TokenType::Semicolon, "Expected ';' after interface method declaration");

    InterfaceMethodDecl md;
    md.name = mName;
    md.params = std::move(params);
    md.retType = std::move(ret);
    iface.methods.push_back(std::move(md));
  }
  expect(TokenType::RBrace, "Expected '}' to end interface body");

  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(iface);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Enum
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseEnum() {
  Token kw = expect(TokenType::KwEnum, "Expected 'enum'");
  Token name = expect(TokenType::Identifier, "Expected enum name");
  expect(TokenType::LBrace, "Expected '{' to start enum body");

  EnumDeclStmt en;
  en.name = name;
  while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
    Token vName = expect(TokenType::Identifier, "Expected variant name");
    std::optional<ExprPtr> val;
    if (match(TokenType::Equal))
      val = parseExpression();
    en.variants.push_back(EnumVariant{vName, std::move(val)});
    if (!match(TokenType::Comma) && !check(TokenType::RBrace))
      expect(TokenType::Comma, "Expected ',' between enum variants");
  }
  expect(TokenType::RBrace, "Expected '}' to end enum body");

  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(en);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Import / Export
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseImport() {
  Token kw = expect(TokenType::KwImport, "Expected 'import'");
  ImportStmt imp;
  imp.kw = kw;

  // import math;
  // import net.http;
  // import ./utils;
  Token first = expect(TokenType::Identifier, "Expected module name after 'import'");
  imp.path.push_back(first);
  while (match(TokenType::Dot)) {
    Token part = expect(TokenType::Identifier, "Expected module path component");
    imp.path.push_back(part);
  }

  // import math as m;
  if (match(TokenType::KwAs)) {
    imp.alias = expect(TokenType::Identifier, "Expected alias name");
  }

  expect(TokenType::Semicolon, "Expected ';' after import");

  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(imp);
  return makeStmt(kw.pos, std::move(s));
}

StmtPtr Parser::parseExport() {
  Token kw = expect(TokenType::KwExport, "Expected 'export'");
  StmtPtr decl = parseDeclOrStmt();
  ExportStmt ex;
  ex.kw = kw;
  ex.decl = std::move(decl);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(ex);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Spawn / Unsafe
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseSpawn() {
  Token kw = expect(TokenType::KwSpawn, "Expected 'spawn'");
  StmtPtr body = parseBlock();
  SpawnStmt sp;
  sp.kw = kw;
  sp.body = std::move(body);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(sp);
  return makeStmt(kw.pos, std::move(s));
}

StmtPtr Parser::parseUnsafe() {
  Token kw = expect(TokenType::KwUnsafe, "Expected 'unsafe'");
  StmtPtr body = parseBlock();
  UnsafeBlock ub;
  ub.kw = kw;
  ub.body = std::move(body);
  Stmt s;
  s.pos = kw.pos;
  s.node = std::move(ub);
  return makeStmt(kw.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Expression statement
// ═══════════════════════════════════════════════════

StmtPtr Parser::parseExprStmt() {
  ExprPtr e = parseExpression();
  expect(TokenType::Semicolon, "Expected ';' after expression");
  SourcePos pos = e ? e->pos : peek().pos;
  ExprStmt es;
  es.expr = std::move(e);
  Stmt s;
  s.pos = pos;
  s.node = std::move(es);
  return makeStmt(s.pos, std::move(s));
}

// ═══════════════════════════════════════════════════
//  Expressions — precedence climbing
// ═══════════════════════════════════════════════════

ExprPtr Parser::parseExpression() { return parseAssignment(); }

ExprPtr Parser::parseAssignment() {
  ExprPtr expr = parseTernary();
  Token t = peek();
  if (t.type == TokenType::Equal || t.type == TokenType::PlusEqual ||
      t.type == TokenType::MinusEqual || t.type == TokenType::StarEqual ||
      t.type == TokenType::SlashEqual || t.type == TokenType::PercentEqual) {
    Token op = next();
    ExprPtr value = parseAssignment(); // right-associative
    AssignExpr asn{std::move(expr), op, std::move(value)};
    Expr out;
    out.pos = op.pos;
    out.node = std::move(asn);
    return makeExpr(op.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseTernary() {
  ExprPtr expr = parseLogicalOr();
  if (check(TokenType::Question)) {
    next();
    ExprPtr thenE = parseExpression();
    expect(TokenType::Colon, "Expected ':' in ternary expression");
    ExprPtr elseE = parseTernary();
    TernaryExpr te;
    te.cond = std::move(expr);
    te.thenExpr = std::move(thenE);
    te.elseExpr = std::move(elseE);
    Expr out;
    out.pos = te.cond->pos;
    out.node = std::move(te);
    return makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseLogicalOr() {
  ExprPtr expr = parseLogicalAnd();
  while (check(TokenType::PipePipe)) {
    Token op = next();
    ExprPtr rhs = parseLogicalAnd();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseLogicalAnd() {
  ExprPtr expr = parseBitwiseOr();
  while (check(TokenType::AmpAmp)) {
    Token op = next();
    ExprPtr rhs = parseBitwiseOr();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseBitwiseOr() {
  ExprPtr expr = parseBitwiseXor();
  while (check(TokenType::Pipe)) {
    Token op = next();
    ExprPtr rhs = parseBitwiseXor();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseBitwiseXor() {
  ExprPtr expr = parseBitwiseAnd();
  while (check(TokenType::Caret)) {
    Token op = next();
    ExprPtr rhs = parseBitwiseAnd();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseBitwiseAnd() {
  ExprPtr expr = parseEquality();
  while (check(TokenType::Amp)) {
    Token op = next();
    ExprPtr rhs = parseEquality();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseEquality() {
  ExprPtr expr = parseComparison();
  while (check(TokenType::EqualEqual) || check(TokenType::BangEqual)) {
    Token op = next();
    ExprPtr rhs = parseComparison();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseComparison() {
  ExprPtr expr = parseShift();
  while (check(TokenType::Less) || check(TokenType::LessEqual) ||
         check(TokenType::Greater) || check(TokenType::GreaterEqual)) {
    Token op = next();
    ExprPtr rhs = parseShift();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseShift() {
  ExprPtr expr = parseTerm();
  while (check(TokenType::LessLess) || check(TokenType::GreaterGreater)) {
    Token op = next();
    ExprPtr rhs = parseTerm();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseTerm() {
  ExprPtr expr = parseFactor();
  while (check(TokenType::Plus) || check(TokenType::Minus)) {
    Token op = next();
    ExprPtr rhs = parseFactor();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseFactor() {
  ExprPtr expr = parsePower();
  while (check(TokenType::Star) || check(TokenType::Slash) ||
         check(TokenType::TildeSlash) || check(TokenType::Percent)) {
    Token op = next();
    ExprPtr rhs = parsePower();
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    expr = makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parsePower() {
  ExprPtr expr = parseUnary();
  if (check(TokenType::StarStar)) {
    Token op = next();
    ExprPtr rhs = parsePower(); // right-associative
    BinaryExpr be{std::move(expr), op, std::move(rhs)};
    Expr out;
    out.pos = be.lhs->pos;
    out.node = std::move(be);
    return makeExpr(out.pos, std::move(out));
  }
  return expr;
}

ExprPtr Parser::parseUnary() {
  if (check(TokenType::Bang) || check(TokenType::Minus) ||
      check(TokenType::Plus) || check(TokenType::Tilde)) {
    Token op = next();
    ExprPtr rhs = parseUnary();
    UnaryExpr ue{op, std::move(rhs)};
    Expr out;
    out.pos = op.pos;
    out.node = std::move(ue);
    return makeExpr(out.pos, std::move(out));
  }
  if (check(TokenType::KwAwait)) {
    Token kw = next();
    ExprPtr expr = parseUnary();
    AwaitExpr ae;
    ae.kw = kw;
    ae.expr = std::move(expr);
    Expr out;
    out.pos = kw.pos;
    out.node = std::move(ae);
    return makeExpr(out.pos, std::move(out));
  }
  if (check(TokenType::KwNew)) {
    Token kw = next();
    Token className = expect(TokenType::Identifier, "Expected class name after 'new'");
    expect(TokenType::LParen, "Expected '(' after class name");
    std::vector<ExprPtr> args;
    if (!check(TokenType::RParen)) {
      for (;;) {
        args.push_back(parseExpression());
        if (!match(TokenType::Comma))
          break;
      }
    }
    expect(TokenType::RParen, "Expected ')'");
    NewExpr ne;
    ne.className = className;
    ne.args = std::move(args);
    Expr out;
    out.pos = kw.pos;
    out.node = std::move(ne);
    return makeExpr(out.pos, std::move(out));
  }
  return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
  ExprPtr expr = parsePrimary();
  for (;;) {
    if (check(TokenType::LParen)) {
      // Function call
      Token lp = next();
      std::vector<ExprPtr> args;
      if (!check(TokenType::RParen)) {
        for (;;) {
          args.push_back(parseExpression());
          if (!match(TokenType::Comma))
            break;
        }
      }
      expect(TokenType::RParen, "Expected ')' after arguments");
      CallExpr ce{std::move(expr), lp, std::move(args)};
      Expr out;
      out.pos = lp.pos;
      out.node = std::move(ce);
      expr = makeExpr(out.pos, std::move(out));
    } else if (check(TokenType::Dot)) {
      // Member access — accept identifiers AND keywords (get, set, length, etc.)
      Token dot = next();
      Token member = peek();
      if (member.type == TokenType::Identifier ||
          (static_cast<int>(member.type) >= static_cast<int>(TokenType::KwVar) &&
           static_cast<int>(member.type) <= static_cast<int>(TokenType::KwUnsafe))) {
        next(); // consume the member token
        // Treat keyword tokens as identifiers for member access
        member.type = TokenType::Identifier;
      } else {
        throw ParseError(member, "Expected member name after '.'");
      }
      MemberExpr me;
      me.object = std::move(expr);
      me.dot = dot;
      me.member = member;
      Expr out;
      out.pos = dot.pos;
      out.node = std::move(me);
      expr = makeExpr(out.pos, std::move(out));
    } else if (check(TokenType::LBracket)) {
      // Index access
      Token lb = next();
      ExprPtr index = parseExpression();
      expect(TokenType::RBracket, "Expected ']' after index");
      IndexExpr ie;
      ie.object = std::move(expr);
      ie.lbracket = lb;
      ie.index = std::move(index);
      Expr out;
      out.pos = lb.pos;
      out.node = std::move(ie);
      expr = makeExpr(out.pos, std::move(out));
    } else {
      break;
    }
  }
  return expr;
}

ExprPtr Parser::parsePrimary() {
  Token t = peek();

  // Literals
  if (match(TokenType::IntLiteral) || match(TokenType::FloatLiteral) ||
      match(TokenType::StringLiteral) || match(TokenType::CharLiteral) ||
      match(TokenType::KwTrue) || match(TokenType::KwFalse) ||
      match(TokenType::KwNull)) {
    LiteralExpr lit{t};
    Expr out;
    out.pos = t.pos;
    out.node = std::move(lit);
    return makeExpr(t.pos, std::move(out));
  }

  // self
  if (match(TokenType::KwSelf)) {
    SelfExpr se{t};
    Expr out;
    out.pos = t.pos;
    out.node = std::move(se);
    return makeExpr(t.pos, std::move(out));
  }

  // super
  if (match(TokenType::KwSuper)) {
    SuperExpr se{t};
    Expr out;
    out.pos = t.pos;
    out.node = std::move(se);
    return makeExpr(t.pos, std::move(out));
  }

  // Identifier
  if (match(TokenType::Identifier)) {
    IdentExpr id{t};
    Expr out;
    out.pos = t.pos;
    out.node = std::move(id);
    return makeExpr(t.pos, std::move(out));
  }

  // Array literal: [1, 2, 3]
  if (check(TokenType::LBracket)) {
    Token lb = next();
    std::vector<ExprPtr> elements;
    if (!check(TokenType::RBracket)) {
      for (;;) {
        elements.push_back(parseExpression());
        if (!match(TokenType::Comma))
          break;
      }
    }
    expect(TokenType::RBracket, "Expected ']' after array elements");
    ArrayExpr ae;
    ae.lbracket = lb;
    ae.elements = std::move(elements);
    Expr out;
    out.pos = lb.pos;
    out.node = std::move(ae);
    return makeExpr(out.pos, std::move(out));
  }

  // Map literal: {"key": value} or block
  // Disambiguate: if { is followed by string:, it's a map; otherwise it's parsed elsewhere
  if (check(TokenType::LBrace)) {
    // Peek ahead to see if this is a map literal (starts with key: value)
    // Simple heuristic: { StringLiteral : ... } is map
    // We need to be careful: don't consume tokens
    // For simplicity, a map literal is { expr : expr, ... }
    // But { could also be a block statement. We handle this by only treating
    // it as a map in expression context when the first thing after { is a string/ident followed by :
    Token lb = next();
    if (check(TokenType::RBrace)) {
      // Empty map
      next();
      MapExpr me;
      me.lbrace = lb;
      Expr out;
      out.pos = lb.pos;
      out.node = std::move(me);
      return makeExpr(out.pos, std::move(out));
    }
    // Try to parse as map: first element must be expr : expr
    ExprPtr firstKey = parseExpression();
    if (check(TokenType::Colon)) {
      // It's a map literal
      next();
      ExprPtr firstVal = parseExpression();
      std::vector<MapEntry> entries;
      entries.push_back(MapEntry{std::move(firstKey), std::move(firstVal)});
      while (match(TokenType::Comma)) {
        if (check(TokenType::RBrace)) break;
        ExprPtr k = parseExpression();
        expect(TokenType::Colon, "Expected ':' in map literal");
        ExprPtr v = parseExpression();
        entries.push_back(MapEntry{std::move(k), std::move(v)});
      }
      expect(TokenType::RBrace, "Expected '}' after map literal");
      MapExpr me;
      me.lbrace = lb;
      me.entries = std::move(entries);
      Expr out;
      out.pos = lb.pos;
      out.node = std::move(me);
      return makeExpr(out.pos, std::move(out));
    }
    // Not a map — this shouldn't normally happen in expression context
    // since blocks are parsed as statements. But we handle gracefully.
    throw ParseError(peek(), "Expected ':' for map literal or statement in expression context");
  }

  // Parenthesized expression or lambda
  if (check(TokenType::LParen)) {
    Token lp = next();

    // Check if this is a lambda: (params) -> retType => body or (params) => body
    // Heuristic: if we see () => or (ident, ...) => or (ident: type, ...) =>
    // We need backtracking, but since we don't have it, we use a simple approach:
    //
    // If immediately ) => or ) -> , it's a lambda with no/one params
    if (check(TokenType::RParen)) {
      next(); // consume )
      if (check(TokenType::FatArrow) || check(TokenType::Arrow)) {
        // Lambda with 0 params
        std::optional<TypeAnn> retType;
        if (match(TokenType::Arrow))
          retType = parseTypeAnn();
        expect(TokenType::FatArrow, "Expected '=>' in lambda");

        LambdaExpr le;
        le.retType = std::move(retType);
        if (check(TokenType::LBrace)) {
          le.bodyBlock = parseBlock();
        } else {
          le.bodyExpr = parseExpression();
        }
        Expr out;
        out.pos = lp.pos;
        out.node = std::move(le);
        return makeExpr(out.pos, std::move(out));
      }
      // Empty group () — return null-like group
      // Actually this is rare, treat it as unit/void
      LiteralExpr lit;
      lit.tok.type = TokenType::KwNull;
      lit.tok.lexeme = "null";
      lit.tok.pos = lp.pos;
      Expr out;
      out.pos = lp.pos;
      out.node = std::move(lit);
      return makeExpr(out.pos, std::move(out));
    }

    // Try to detect lambda: parse the expression inside parens,
    // then check if what follows ) is => or ->
    // Save position conceptually — we parse interior as expression
    ExprPtr inner = parseExpression();

    // Check for comma (multiple params → lambda)
    if (check(TokenType::Comma)) {
      // This is a lambda with multiple params
      std::vector<Token> params;
      std::vector<std::optional<TypeAnn>> paramTypes;
      // Extract first param from inner expression
      if (auto* id = std::get_if<IdentExpr>(&inner->node)) {
        params.push_back(id->name);
        paramTypes.push_back(std::nullopt);
      } else {
        throw ParseError(lp, "Expected parameter name in lambda");
      }
      while (match(TokenType::Comma)) {
        Token pName = expect(TokenType::Identifier, "Expected parameter name");
        std::optional<TypeAnn> pType;
        if (match(TokenType::Colon)) {
          pType = parseTypeAnn();
        }
        params.push_back(pName);
        paramTypes.push_back(std::move(pType));
      }
      expect(TokenType::RParen, "Expected ')' after lambda params");

      std::optional<TypeAnn> retType;
      if (match(TokenType::Arrow))
        retType = parseTypeAnn();
      expect(TokenType::FatArrow, "Expected '=>' in lambda");

      LambdaExpr le;
      le.params = std::move(params);
      le.paramTypes = std::move(paramTypes);
      le.retType = std::move(retType);
      if (check(TokenType::LBrace)) {
        le.bodyBlock = parseBlock();
      } else {
        le.bodyExpr = parseExpression();
      }
      Expr out;
      out.pos = lp.pos;
      out.node = std::move(le);
      return makeExpr(out.pos, std::move(out));
    }

    expect(TokenType::RParen, "Expected ')'");

    // Check if this is a lambda: ) followed by => or ->
    if (check(TokenType::FatArrow) || check(TokenType::Arrow)) {
      std::vector<Token> params;
      std::vector<std::optional<TypeAnn>> paramTypes;
      if (auto* id = std::get_if<IdentExpr>(&inner->node)) {
        params.push_back(id->name);
        paramTypes.push_back(std::nullopt);
      } else {
        throw ParseError(lp, "Expected parameter name in lambda");
      }

      std::optional<TypeAnn> retType;
      if (match(TokenType::Arrow))
        retType = parseTypeAnn();
      expect(TokenType::FatArrow, "Expected '=>' in lambda");

      LambdaExpr le;
      le.params = std::move(params);
      le.paramTypes = std::move(paramTypes);
      le.retType = std::move(retType);
      if (check(TokenType::LBrace)) {
        le.bodyBlock = parseBlock();
      } else {
        le.bodyExpr = parseExpression();
      }
      Expr out;
      out.pos = lp.pos;
      out.node = std::move(le);
      return makeExpr(out.pos, std::move(out));
    }

    // Regular grouped expression
    GroupExpr ge{std::move(inner)};
    Expr out;
    out.pos = lp.pos;
    out.node = std::move(ge);
    return makeExpr(out.pos, std::move(out));
  }

  throw ParseError(t, "Expected expression");
}

} // namespace nova
