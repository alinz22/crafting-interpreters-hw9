#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "scanner.h"

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_CONDITIONAL, // ?:
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

static const char* precedenceNames[] = {
  "PREC_NONE", "PREC_ASSIGNMENT", "PREC_CONDITIONAL", "PREC_OR", "PREC_AND",
  "PREC_EQUALITY", "PREC_COMPARISON", "PREC_TERM", "PREC_FACTOR",
  "PREC_UNARY", "PREC_CALL", "PREC_PRIMARY"
};

typedef void (*ParseFn)(void);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

Parser parser;
Chunk* compilingChunk;
bool traceParser = false;
static int traceDepth = 0;

static void traceEnter(const char* name, const char* detail) {
  if (!traceParser) return;
  for (int i = 0; i < traceDepth; i++) printf("| ");
  printf("%s%s\n", name, detail);
  traceDepth++;
}

static void traceExit(void) {
  if (traceParser) traceDepth--;
}

static Chunk* currentChunk(void) {
  return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance(void) {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitReturn(void) {
  emitByte(OP_RETURN);
}

static void emitConstant(Value value) {
  writeConstant(currentChunk(), value, parser.previous.line);
}

static void endCompiler(void) {
  emitReturn();
  if (traceParser && !parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
}

static void expression(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary(void) {
  TokenType operatorType = parser.previous.type;
  char detail[32];
  snprintf(detail, sizeof(detail), " for \"%.*s\"", parser.previous.length, parser.previous.start);
  traceEnter("binary", detail);
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitByte(OP_NOT_EQUAL); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitByte(OP_LESS_EQUAL); break;
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    default: break;
  }
  traceExit();
}

static void literal(void) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL:   emitByte(OP_NIL); break;
    case TOKEN_TRUE:  emitByte(OP_TRUE); break;
    default: break;
  }
}

static void conditional(void) {
  traceEnter("conditional", "");
  parsePrecedence(PREC_ASSIGNMENT);
  consume(TOKEN_COLON, "Expect ':' after then branch of conditional.");
  parsePrecedence(PREC_CONDITIONAL);
  emitByte(OP_CONDITIONAL);
  traceExit();
}

static void grouping(void) {
  traceEnter("grouping", "");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
  traceExit();
}

static void number(void) {
  char detail[64];
  snprintf(detail, sizeof(detail), " %.*s", parser.previous.length, parser.previous.start);
  traceEnter("number", detail);
  double value = strtod(parser.previous.start, NULL);
  if (value == 0 && !signbit(value)) {
    emitByte(OP_ZERO);
  } else if (value == 1) {
    emitByte(OP_ONE);
  } else {
    emitConstant(NUMBER_VAL(value));
  }
  traceExit();
}

static void string(void) {
  emitConstant(OBJ_VAL(constantString(parser.previous.start + 1,
                                  parser.previous.length - 2)));
}

static void unary(void) {
  TokenType operatorType = parser.previous.type;
  char detail[32];
  snprintf(detail, sizeof(detail), " for \"%.*s\"", parser.previous.length, parser.previous.start);
  traceEnter("unary", detail);
  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
    case TOKEN_BANG:  emitByte(OP_NOT); break;
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default: break;
  }
  traceExit();
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,        PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,        PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,        PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,        PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,        PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary,      PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary,      PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,        PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary,      PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary,      PREC_FACTOR},
  [TOKEN_QUESTION]      = {NULL,     conditional, PREC_CONDITIONAL},
  [TOKEN_COLON]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_BANG]          = {unary,    NULL,        PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary,      PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,      PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary,      PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary,      PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary,      PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary,      PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {NULL,     NULL,        PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,        PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,        PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,        PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,        PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,        PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,        PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,        PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,        PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,        PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,        PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,        PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,        PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,        PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,        PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,        PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,        PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
  char detail[32];
  snprintf(detail, sizeof(detail), "(%s)", precedenceNames[precedence]);
  traceEnter("parsePrecedence", detail);

  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    traceExit();
    return;
  }

  prefixRule();

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule();
  }
  traceExit();
}

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void expression(void) {
  traceEnter("expression", "");
  parsePrecedence(PREC_ASSIGNMENT);
  traceExit();
}

bool compile(const char* source, Chunk* chunk) {
  initScanner(source);
  compilingChunk = chunk;
  traceDepth = 0;

  parser.hadError = false;
  parser.panicMode = false;

  advance();
  expression();
  consume(TOKEN_EOF, "Expect end of expression.");
  endCompiler();
  return !parser.hadError;
}
