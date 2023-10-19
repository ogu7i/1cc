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
    if (*p == '+' || *p == '-') {
      cur = cur->next = new_token(TK_PUNCT, p, p + 1);
      p++;
      continue;
    }

    error_at(p, "不正なトークンです");
  }

  cur = cur->next = new_token(TK_EOF, p, p);
  return head.next;
}

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: 引数の個数が正しくありません\n", argv[0]);

  current_input = argv[1];
  Token *tok = tokenize();

  printf(".intel_syntax noprefix\n");
  printf("  .globl main\n");
  printf("main:\n");

  // 最初のトークンは数値
  printf("  mov rax, %d\n", get_number(tok));
  tok = tok->next;

  // その後は "+ <数値>" か "- <数値>"が続く
  while (tok->kind != TK_EOF) {
    if (equal(tok, "+")) {
      tok = tok->next;
      printf("  add rax, %d\n", get_number(tok));
      tok = tok->next;
      continue;
    }

    tok = skip(tok, "-");
    printf("  sub rax, %d\n", get_number(tok));
    tok = tok->next;
  }

  printf("  ret\n");
  return 0;
}

