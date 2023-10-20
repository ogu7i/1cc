#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  TK_PUNCT, // 記号
  TK_NUM,   // 数値
  TK_EOF,   // EOF
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

// 入力文字列
static char *current_input;

// エラー報告とexit
static void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

// エラーの位置の報告とexit
static void verror_at(char *loc, char *fmt, va_list ap) {
  int pos = loc - current_input;
  fprintf(stderr, "%s\n", current_input);
  fprintf(stderr, "%*s", pos, "");
  fprintf(stderr, "^ ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

static void error_at(char *loc, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror_at(loc, fmt, ap);
}

static void error_tok(Token *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror_at(tok->loc, fmt, ap);
}

// 現在のトークンがsかどうか
static bool equal(Token *tok, char *s) {
  return memcmp(tok->loc, s, tok->len) == 0 && s[tok->len] == '\0';
}

// 現在のトークンがsなら次のトークンを返す
static Token *skip(Token *tok, char *s) {
  if (!equal(tok, s))
    error_tok(tok, "'%s'ではありません", s);

  return tok->next;
}

// 現在のトークンが数値ならその値を返す
static int get_number(Token *tok) {
  if (tok->kind != TK_NUM)
    error_tok(tok, "数値ではありません");

  return tok->val;
}

// 新しいトークンを作る
static Token *new_token(TokenKind kind, char *start, char *end) {
  Token *tok = calloc(1, sizeof(Token));
  tok->kind = kind;
  tok->loc = start;
  tok->len = end - start;
  return tok;
}

// current_inputをトークナイズしてトークン列を返す
static Token *tokenize(void) {
  char *p = current_input;
  Token head = {};
  Token *cur = &head;

  while (*p) {
    // 空白文字はスキップ
    if (isspace(*p)) {
      p++;
      continue;
    }

    // 数値
    if (isdigit(*p)) {
      cur = cur->next = new_token(TK_NUM, p, p);
      char *q = p;
      cur->val = strtoul(p, &p, 10);
      cur->len = p - q;
      continue;
    }

    // 記号
    if (ispunct(*p)) {
      cur = cur->next = new_token(TK_PUNCT, p, p + 1);
      p++;
      continue;
    }

    error_at(p, "不正なトークンです");
  }

  cur = cur->next = new_token(TK_EOF, p, p);
  return head.next;
}

typedef enum {
  ND_ADD, // +
  ND_SUB, // -
  ND_MUL, // *
  ND_DIV, // /
  ND_NEG, // 単項 -
  ND_NUM, // 整数
} NodeKind;

typedef struct Node Node;
struct Node {
  NodeKind kind; // ノードの種類
  Node *lhs;     // 左側のノード
  Node *rhs;     // 右側
  int val;       // ノードがND_NUMのときに使う。数値。
};

// 新しいノードを作る。種類をセットするだけ。
static Node *new_node(NodeKind kind) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  return node;
}

// 新しい2分木ノードを作る。
static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
  Node *node = new_node(kind);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

// 新しい単項ノードを作る。
static Node *new_unary(NodeKind kind, Node *expr) {
  Node *node = new_node(kind);
  node->lhs = expr;
  return node;
}

// 新しい数値ノードを作る。
static Node *new_num(int val) {
  Node *node = new_node(ND_NUM);
  node->val = val;
  return node;
}

// 再帰下降構文解析によるパースのための関数群
static Node *expr(Token **rest, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);

// expr = mul ("+" mul | "-" mul)*
static Node *expr(Token **rest, Token *tok) {
  Node *node = mul(&tok, tok);

  for (;;) {
    if (equal(tok, "+")) {
      node = new_binary(ND_ADD, node, mul(&tok, tok->next));
      continue;
    }

    if (equal(tok, "-")) {
      node = new_binary(ND_SUB, node, mul(&tok, tok->next));
      continue;
    }

    *rest = tok;
    return node;
  }
}

// mul = unary ("*" unary | "/" unary)*
static Node *mul(Token **rest, Token *tok) {
  Node *node = unary(&tok, tok);

  for (;;) {
    if (equal(tok, "*")) {
      node = new_binary(ND_MUL, node, unary(&tok, tok->next));
      continue;
    }

    if (equal(tok, "/")) {
      node = new_binary(ND_DIV, node, unary(&tok, tok->next));
      continue;
    }

    *rest = tok;
    return node;
  }
}

// unary = ("+" | "-") unary 
//       | primary
static Node *unary(Token **rest, Token *tok) {
  if (equal(tok, "+"))
    return unary(rest, tok->next);

  if (equal(tok, "-"))
    return new_unary(ND_NEG, unary(rest, tok->next));

  return primary(rest, tok);
}

// primary = "(" expr ")" | num
static Node *primary(Token **rest, Token *tok) {
  if (equal(tok, "(")) {
    Node *node = expr(&tok, tok->next);
    *rest = skip(tok, ")");
    return node;
  }

  if (tok->kind == TK_NUM) {
    Node *node = new_num(tok->val);
    *rest = tok->next;
    return node;
  }

  error_tok(tok, "式でないといけません");
}

// コード生成

static int depth;

static void push(void) {
  printf("  push rax\n");
  depth++;
}

static void pop(char *arg) {
  printf("  pop %s\n", arg);
  depth--;
}

static void gen_expr(Node *node) {
  switch (node->kind) {
    case ND_NUM:
      printf("  mov rax, %d\n", node->val);
      return;
    case ND_NEG:
      gen_expr(node->lhs);
      printf("  neg rax\n");
      return;
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("rdi");

  switch (node->kind) {
    case ND_ADD:
      printf("  add rax, rdi\n");
      return;
    case ND_SUB:
      printf("  sub rax, rdi\n");
      return;
    case ND_MUL:
      printf("  imul rax, rdi\n");
      return;
    case ND_DIV:
      printf("  cqo\n");
      printf("  idiv rdi\n");
      return;
  }

  error("不正な式です");
}

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: 引数の個数が正しくありません\n", argv[0]);

  // トークナイズとパース
  current_input = argv[1];
  Token *tok = tokenize();
  Node *node = expr(&tok, tok);

  if (tok->kind != TK_EOF)
    error_tok(tok, "余分なトークンがあります");

  printf(".intel_syntax noprefix\n");
  printf("  .globl main\n");
  printf("main:\n");

  // ASTをたどってアセンブリを生成
  gen_expr(node);
  printf("  ret\n");

  // pushとpopの数が合ってるかチェック
  assert(depth == 0);
  return 0;
}

