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
static void verror_at(int line_no, char *loc, char *fmt, va_list ap) {
  char *line = loc;
  // locが含まれている行頭を取得
  while (current_input < line && line[-1] != '\n')
    line--;

  // locが含まれている行末を取得
  char *end = loc;
  while (*end != '\n')
    end++;

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
  int line_no = 1;
  for (char *p = current_input; p < loc; p++)
    if (*p == '\n')
      line_no++;

  va_list ap;
  va_start(ap, fmt);
  verror_at(line_no, loc, fmt, ap);
}

void error_tok(Token *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror_at(tok->line_no, tok->loc, fmt, ap);
}

// すべてのトークンの行番号をセット
static void add_line_numbers(Token *tok) {
  char *p = current_input;
  int n = 1;

  do {
    if (p == tok->loc) {
      tok->line_no = n;
      tok = tok->next;
    }
    if (*p == '\n')
      n++;
  } while (*p++);
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
  static char *kw[] = {
    "==", "!=", "<=", ">=", "->", "+=", "-=", "*=", "/=",
    "++", "--", "%=",
  };

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
    if(startswith(p, kw[i]))
      return strlen(kw[i]);

  return ispunct(*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
  static char *kw[] = {
    "return", "if", "else", "while", "for", "int", "sizeof", 
    "char", "struct", "union", "long", "short", "void", 
    "typedef", "_Bool", "enum", "static"
  };

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

static int from_hex(char c) {
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  
  return c - 'A' + 10;
}

static int read_escaped_char(char **new_pos, char *p) {
  // 8進数
  if ('0' <= *p && *p <= '7') {
    int c = *p++ - '0';
    if ('0' <= *p && *p <= '7') {
      c = (c << 3) + (*p++ - '0');
      if ('0' <= *p && *p <= '7')
        c = (c << 3) + (*p++ - '0');
    }

    *new_pos = p;
    return c;
  }

  // 16進数
  if (*p == 'x') {
    p++;
    if (!isxdigit(*p))
      error_at(p, "不正な16進エスケープシーケンスです");

    int c = 0;
    for (; isxdigit(*p); p++)
      c = (c << 4) + from_hex(*p);

    *new_pos = p;
    return c;
  }

  *new_pos = p + 1;

  switch (*p) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 't': return '\t';
    case 'n': return '\n';
    case 'v': return '\v';
    case 'f': return '\f';
    case 'r': return '\r';
    case 'e': return 27;
    default: return *p;
  }
}

static char *string_literal_end(char *p) {
  char *start = p;
  for (; *p != '"'; p++){
    if (*p == '\n' || *p == '\0')
      error_at(start, "閉じられていない文字列リテラルです");
    if (*p == '\\')
      p++;
  }

  return p;
}

static Token *read_string_literal(char *start) {
  char *end = string_literal_end(start + 1);
  char *buf = calloc(1, end - start);
  int len = 0;

  for (char *p = start + 1; p < end;) {
    if (*p == '\\')
      buf[len++] = read_escaped_char(&p, p + 1);
    else
      buf[len++] = *p++;
  }

  Token *tok = new_token(TK_STR, start, end + 1);
  tok->ty = array_of(ty_char, len + 1);
  tok->str = buf;
  return tok;
}

static Token *read_char_literal(char *start) {
  char *p = start + 1;
  if (*p == '\0')
    error_at(start, "閉じられていない文字リテラルです");

  char c;
  if (*p == '\\')
    c = read_escaped_char(&p, p + 1);
  else
    c = *p++;

  char *end = strchr(p, '\'');
  if (!end)
    error_at(p, "閉じられていない文字リテラルです");

  Token *tok = new_token(TK_NUM, start, end + 1);
  tok->val = c;
  return tok;
}

static Token *read_int_literal(char *start) {
  char *p = start;

  int base = 10;
  if (!strncasecmp(p, "0x", 2) && isalnum(p[2])) {
    p += 2;
    base = 16;
  } else if (!strncasecmp(p, "0b", 2) && isalnum(p[2])) {
    p += 2;
    base = 2;
  } else if (*p == '0') {
    base = 8;
  }

  long val = strtoul(p, &p, base);
  if (isalnum(*p))
    error_at(p, "不正な数値です");

  Token *tok = new_token(TK_NUM, start, p);
  tok->val = val;
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

    // 文字リテラル
    if (*p == '\'') {
      cur = cur->next = read_char_literal(p);
      p += cur->len;
      continue;
    }

    // 数値
    if (isdigit(*p)) {
      cur = cur->next = read_int_literal(p);
      p += cur->len;
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
  add_line_numbers(head.next);
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

