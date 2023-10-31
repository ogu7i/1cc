#include "1cc.h"

static char *current_input;

// エラー報告とexit
void error(char *fmt, ...) {
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

void error_at(char *loc, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror_at(loc, fmt, ap);
}

void error_tok(Token *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror_at(tok->loc, fmt, ap);
}

// 現在のトークンがsかどうか
bool equal(Token *tok, char *s) {
  return memcmp(tok->loc, s, tok->len) == 0 && s[tok->len] == '\0';
}

// 現在のトークンがsなら次のトークンを返す
Token *skip(Token *tok, char *s) {
  if (!equal(tok, s))
    error_tok(tok, "'%s'ではありません", s);

  return tok->next;
}

// 現在のトークンが`s`ならrestを次のトークンにしてtrueを返す
bool consume(Token **rest, Token *tok, char *s) {
  if (equal(tok, s)) {
    *rest = tok->next;
    return true;
  }

  *rest = tok;
  return false;
}

// 新しいトークンを作る
Token *new_token(TokenKind kind, char *start, char *end) {
  Token *tok = calloc(1, sizeof(Token));
  tok->kind = kind;
  tok->loc = start;
  tok->len = end - start;
  return tok;
}

// 識別子の最初の文字が有効ならtrue
static bool is_ident1(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

// 識別子の2文字目以降が有効ならtrue
static bool is_ident2(char c) {
  return is_ident1(c) || ('0' <= c && c <= '9');
}

static bool startswith(char *p, char *q) {
  return strncmp(p, q, strlen(q)) == 0;
}

// 記号を読んでその長さを返す
static int read_punct(char *p) {
  if (startswith(p, "==") || startswith(p, "!=") ||
      startswith(p, "<=") || startswith(p, ">="))
    return 2;

  return ispunct(*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
  static char *kw[] = {"return", "if", "else", "while", "for", "int", "sizeof", "char"};

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
    if (equal(tok, kw[i]))
      return true;

  return false;
}

// キーワードもTK_IDENTになっているので
// キーワードが見つかったらTK_KEYWORDにする
static void convert_keywords(Token *tok) {
  for (Token *t = tok; t->kind != TK_EOF; t = t->next)
    if (is_keyword(t))
      t->kind = TK_KEYWORD;
}

static Token *read_string_literal(char *start) {
  char *p = start + 1;
  for (; *p != '"'; p++)
    if (*p == '\n' || *p == '\0')
      error_at(start, "文字列リテラルが閉じていません");

  Token *tok = new_token(TK_STR, start, p + 1);
  tok->ty = array_of(ty_char, p - start);
  tok->str = strndup(start + 1, p - start - 1);
  return tok;
}

// pをトークナイズしてトークン列を返す
Token *tokenize(char *p) {
  current_input = p;
  Token head = {};
  Token *cur = &head;

  while (*p) {
    // 空白文字はスキップ
    if (isspace(*p)) {
      p++;
      continue;
    }

    // 文字列リテラル
    if (*p == '"') {
      cur = cur->next = read_string_literal(p);
      p += cur->len;
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

    // 識別子かキーワード
    if (is_ident1(*p)) {
      char *start = p;
      do {
        p++;
      } while (is_ident2(*p));

      cur = cur->next = new_token(TK_IDENT, start, p);
      continue;
    }

    // 記号
    int punct_len = read_punct(p);
    if (punct_len) {
      cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
      p += cur->len;
      continue;
    }

    error_at(p, "不正なトークンです");
  }

  cur = cur->next = new_token(TK_EOF, p, p);
  convert_keywords(head.next);
  return head.next;
}

