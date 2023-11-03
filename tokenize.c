#include "1cc.h"

// 入力ファイル名
static char *current_filename;

// 入力文字列
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
  char *line = loc;
  // locが含まれている行頭を取得
  while (current_input < line && line[-1] != '\n')
    line--;

  // locが含まれている行末を取得
  char *end = loc;
  while (*end != '\n')
    end++;

  // 行数を取得
  int line_no = 1;
  for (char *p = current_input; p < line; p++)
    if (*p == '\n')
      line_no++;

  // 該当行を表示
  int indent = fprintf(stderr, "%s:%d:", current_filename, line_no);
  fprintf(stderr, "%.*s\n", (int)(end - line), line);

  int pos = loc - line + indent;

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
  for (; *p != '"'; p++) {
    if (*p == '\n' || *p == '\0')
      error_at(start, "文字列リテラルが閉じていません");
    if (*p == '\\')
      p++;
  }

  Token *tok = new_token(TK_STR, start, p + 1);
  tok->ty = array_of(ty_char, p - start);
  tok->str = strndup(start + 1, p - start - 1);
  return tok;
}

// pをトークナイズしてトークン列を返す
static Token *tokenize(char *p) {
  current_input = p;
  Token head = {};
  Token *cur = &head;

  while (*p) {
    // 行コメントはスキップ
    if (startswith(p, "//")) {
      p += 2;
      while (*p != '\n')
        p++;
      continue;
    }

    // ブロックコメントはスキップ
    if (startswith(p, "/*")) {
      char *q = strstr(p + 2, "*/");
      if (!q)
        error_at(p, "ブロックコメントが閉じていません");
      p = q + 2;
      continue;
    }

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

// 与えられたファイルの中身を返す
static char *read_file(char *path) {
  FILE *fp;

  if (strcmp(path, "-") == 0) {
    // ファイル名として"-"が与えられた場合は標準入力から読む
    fp = stdin;
  } else {
    fp = fopen(path, "r");
    if (!fp)
      error("%sを開けませんでした: %s", path, strerror(errno));
  }

  char *buf;
  size_t buflen;
  FILE *out = open_memstream(&buf, &buflen);

  // ファイル全体を読む
  for (;;) {
    char buf2[4096];
    int n = fread(buf2, 1, sizeof(buf2), fp);
    if (n == 0)
      break;
    fwrite(buf2, 1, n, out);
  }

  if (fp != stdin)
    fclose(fp);

  // 最終行は必ず'\n'で終わっているように
  fflush(out);
  if (buflen == 0 || buf[buflen - 1] != '\n')
    fputc('\n', out);

  fputc('\0', out);
  fclose(out);
  return buf;
}

Token *tokenize_file(char *path) {
  current_filename = path;
  return tokenize(read_file(path));
}

