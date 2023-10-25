#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Type Type;
typedef struct Node Node;

//
// tokenize.c
//

typedef enum {
  TK_IDENT,   // 識別子
  TK_PUNCT,   // 記号
  TK_KEYWORD, // キーワード
  TK_NUM,     // 数値
  TK_EOF,     // EOF
} TokenKind;

// Token type
typedef struct Token Token;
struct Token {
  TokenKind kind; // トークンの種類
  Token *next;    // 次のトークン
  int val;        // kindがTK_NUMのとき、その数値
  char *loc;      // トークンの位置
  int len;        // トークンの長さ
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *s);
Token *skip(Token *tok, char *s);
bool consume(Token **rest, Token *tok, char *s);
Token *tokenize(char *p);

//
// parse.c
//

// ローカル変数
typedef struct Obj Obj;
struct Obj {
  Obj *next;
  char *name; // 変数名
  Type *ty;   // 型
  int offset; // rbpからのオフセット
};

// 関数
typedef struct Function Function;
struct Function {
  Node *body;     // 関数本体のASTノード
  Obj *locals;    // ローカル変数
  int stack_size; // 変数のために確保するスタックサイズ
};

typedef enum {
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_NEG,       // 単項 -
  ND_ADDR,      // 単項 &
  ND_DEREF,     // 単項 *
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // =
  ND_RETURN,    // return
  ND_IF,        // if
  ND_WHILE,     // while
  ND_FOR,       // for
  ND_BLOCK,     // { ... }
  ND_EXPR_STMT, // 式文
  ND_VAR,       // 変数
  ND_NUM,       // 整数
} NodeKind;

struct Node {
  NodeKind kind; // ノードの種類
  Node *next;    // 次のノード
  Type *ty;      // 型(int, int *)
  Token *tok;    // エラー報告用。代表的なトークン。
  Node *lhs;     // 左辺
  Node *rhs;     // 右辺
  
  // if, while, for(condのみ)
  Node *cond;    // 条件
  Node *then;    // 条件がtrueのとき実行
  Node *els;     // 条件がfalseのとき実行

  // for
  Node *init;    // forの初期化部
  Node *inc;     // forの更新部

  Node *body;    // ブロック

  Obj *var;      // ND_VARのとき使う。変数。
  int val;       // ノードがND_NUMのときに使う。数値。
};

Function *parse(Token *tok);

//
// codegen.c
//

void codegen(Function *prog);

//
// type.c
//

typedef enum {
  TY_INT, // int
  TY_PTR, // pointer
} TypeKind;

struct Type {
  TypeKind kind; // 型の種類(int, ...)
  Type *base;    // ポインタの場合、指してるType
  Token *name;   // 宣言子の識別子
};

extern Type *ty_int;

bool is_integer(Type *ty);
Type *pointer_to(Type *base);
void add_type(Node *node);

