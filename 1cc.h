#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;

//
// tokenize.c
//

typedef enum {
  TK_IDENT,   // 識別子
  TK_PUNCT,   // 記号
  TK_KEYWORD, // キーワード
  TK_NUM,     // 数値
  TK_STR,     // 文字列リテラル
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

  // TK_STR
  Type *ty;       // 型
  char *str;      // 文字列

  int line_no;    // 行番号
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *s);
Token *skip(Token *tok, char *s);
bool consume(Token **rest, Token *tok, char *s);
Token *tokenize_file(char *filename);

//
// parse.c
//

// 変数 / 関数
typedef struct Obj Obj;
struct Obj {
  Obj *next;
  char *name;       // 変数名
  Type *ty;         // 型
  bool is_local;    // ローカルかグローバルか

  // ローカル変数用
  int offset;       // rbpからのオフセット

  // グローバル変数 / 関数用
  bool is_function; // 関数かグローバル変数か

  // グローバル変数
  char *init_data;  // 初期化のためのデータ

  // 関数用
  Obj *params;      // 関数の仮引数
  Node *body;       // 関数のbodyのAST
  Obj *locals;      // ローカル変数
  int stack_size;   // 変数と引数のためのスタックサイズ
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
  ND_COMMA,     // , コンマ演算子
  ND_MEMBER,    // . 構造体のメンバアクセス
  ND_RETURN,    // return
  ND_IF,        // if
  ND_WHILE,     // while
  ND_FOR,       // for
  ND_BLOCK,     // { ... }
  ND_FUNCALL,   // 関数呼び出し
  ND_EXPR_STMT, // 式文
  ND_STMT_EXPR, // Statement expression
  ND_VAR,       // 変数
  ND_NUM,       // 整数
} NodeKind;

struct Node {
  NodeKind kind;  // ノードの種類
  Node *next;     // 次のノード
  Type *ty;       // 型(int, int *)
  Token *tok;     // エラー報告用。代表的なトークン。
  Node *lhs;      // 左辺
  Node *rhs;      // 右辺
  
  // if, while, for(condのみ)
  Node *cond;     // 条件
  Node *then;     // 条件がtrueのとき実行
  Node *els;      // 条件がfalseのとき実行

  // for
  Node *init;     // forの初期化部
  Node *inc;      // forの更新部

  Node *body;     // ブロックかstatement expression

  // 構造体
  Member *member; // 構造体のメンバ

  // 関数
  char *funcname; // 関数名
  Node *args;     // 実引数

  Obj *var;       // ND_VARのとき使う。変数。
  int val;        // ノードがND_NUMのときに使う。数値。
};

Obj *parse(Token *tok);

//
// codegen.c
//

void codegen(Obj *prog, FILE *out);

//
// type.c
//

typedef enum {
  TY_CHAR,   // char
  TY_INT,    // int
  TY_PTR,    // pointer
  TY_FUNC,   // 関数
  TY_ARRAY,  // 配列
  TY_STRUCT, // 構造体
} TypeKind;

struct Type {
  TypeKind kind;   // 型の種類(int, ...)
  int size;        // sizeofで返される値
  Type *base;      // ポインタ(配列)の場合、指してるType
  Token *name;     // 宣言子の識別子

  // 配列
  int array_len;

  // 構造体
  Member *members;

  // 関数
  Type *return_ty; // 関数の返り値の型
  Type *params;    // 仮引数
  Type *next;
};

// 構造体のメンバ
struct Member {
  Member *next;
  Type *ty;
  Token *name;
  int offset;
};

extern Type *ty_char;
extern Type *ty_int;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int len);
void add_type(Node *node);

