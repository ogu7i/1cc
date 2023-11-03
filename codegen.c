#include "1cc.h"

// 使ってるスタックの深さ。push/popごとに増減
static int depth;
// 関数呼び出し時に引数をセットするレジスタ群 
static char *argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
// 現在処理している関数
static Obj *current_fn;

// 出力先ファイル
static FILE *output_file;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

// ラベル用カウンタ
static int count(void) {
  static int i = 1;
  return i++;
}

static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fprintf(output_file, "\n");
}

static void push(void) {
  println("  push rax");
  depth++;
}

static void pop(char *arg) {
  println("  pop %s", arg);
  depth--;
}

// nを`align`の最も近い倍数に丸める。
// 例えば、align_to(5, 8)だと8に、align(11, 8)だと16になる。
static int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// 与えられたノードの絶対アドレスを計算する
// ノードがメモリ内になければエラー
static void gen_addr(Node *node) {
  if (node->kind == ND_VAR) {
    if (node->var->is_local)
      println("  lea rax, [rbp-%d]", node->var->offset);
    else
      println("  lea rax, %s[rip]", node->var->name);

    return;
  }

  if (node->kind == ND_DEREF) {
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "左辺値ではありません");
}

static void load(Type *ty) {
  if (ty->kind == TY_ARRAY)
    return;

  if (ty->size == 1)
    println("  movsbq rax, [rax]");
  else
    println("  mov rax, [rax]");
}

static void store(Type *ty) {
  pop("rdi");

  if (ty->size == 1)
    println("  mov [rdi], al");
  else
    println("  mov [rdi], rax");
}

static void gen_expr(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);

  switch (node->kind) {
    case ND_NUM:
      println("  mov rax, %d", node->val);
      return;
    case ND_NEG:
      gen_expr(node->lhs);
      println("  neg rax");
      return;
    case ND_ADDR:
      gen_addr(node->lhs);
      return;
    case ND_DEREF:
      gen_expr(node->lhs);
      load(node->ty);
      return;
    case ND_VAR:
      gen_addr(node);
      load(node->ty);
      return;
    case ND_ASSIGN:
      gen_addr(node->lhs);
      push();
      gen_expr(node->rhs);
      store(node->ty);
      return;
    case ND_STMT_EXPR:
      for (Node *n = node->body; n; n = n->next)
        gen_stmt(n);
      return;
    case ND_FUNCALL: {
      int nargs = 0;
      for (Node *arg = node->args; arg; arg = arg->next) {
        gen_expr(arg);
        push();
        nargs++;
      }

      for (int i = nargs - 1; i >= 0; i--)
        pop(argreg64[i]);

      println("  mov rax, 0");

      if (depth % 2 == 0) {
        println("  call %s", node->funcname);
      } else {
        println("  sub rsp, 8");
        println("  call %s", node->funcname);
        println("  add rsp, 8");
      }

      return;
    }
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("rdi");

  switch (node->kind) {
    case ND_ADD:
      println("  add rax, rdi");
      return;
    case ND_SUB:
      println("  sub rax, rdi");
      return;
    case ND_MUL:
      println("  imul rax, rdi");
      return;
    case ND_DIV:
      println("  cqo");
      println("  idiv rdi");
      return;
    case ND_EQ:
      println("  cmp rax, rdi");
      println("  sete al");
      println("  movzb rax, al");
      return;
    case ND_NE:
      println("  cmp rax, rdi");
      println("  setne al");
      println("  movzb rax, al");
      return;
    case ND_LT:
      println("  cmp rax, rdi");
      println("  setl al");
      println("  movzb rax, al");
      return;
    case ND_LE:
      println("  cmp rax, rdi");
      println("  setle al");
      println("  movzb rax, al");
      return;
  }

  error_tok(node->tok, "不正な式です");
}

static void gen_stmt(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);

  switch (node->kind) {
    case ND_RETURN:
      gen_expr(node->lhs);
      println("  jmp .L.return.%s", current_fn->name);
      return;
    case ND_EXPR_STMT:
      gen_expr(node->lhs);
      return;
    case ND_BLOCK:
      for (Node *n = node->body; n; n = n->next)
        gen_stmt(n);
      return;
    case ND_IF: {
      int c = count();

      gen_expr(node->cond);
      println("  cmp rax, 0");
      println("  je .L.else.%d", c);

      gen_stmt(node->then);
      println("  jmp .L.end.%d", c);

      println(".L.else.%d:", c);
      if (node->els) {
        gen_stmt(node->els);
      }

      println(".L.end.%d:", c);
      return;
    }
    case ND_WHILE: {
      int c = count();

      println(".L.begin.%d:", c);
      gen_expr(node->cond);
      println("  cmp rax, 0");
      println("  je .L.end.%d", c);
      gen_stmt(node->then);
      println("  jmp .L.begin.%d", c);
      println(".L.end.%d:", c);
      return;
    }
    case ND_FOR: {
      int c = count();
      gen_stmt(node->init);
      println(".L.begin.%d:", c);
      
      if (node->cond) {
        gen_expr(node->cond);
        println("  cmp rax, 0");
        println("  je .L.end.%d", c);
      }

      gen_stmt(node->then);
      
      if (node->inc)
        gen_expr(node->inc);

      println("  jmp .L.begin.%d", c);
      println(".L.end.%d:", c);
      return;
    }
  }

  error_tok(node->tok, "不正な文です");
}

static void assign_lvar_offsets(Obj *fn) {
  int offset = 0;
  for (Obj *var = fn->locals; var; var = var->next) {
    offset += var->ty->size;
    var->offset = offset;
  }

  fn->stack_size = align_to(offset, 16);
}

static int hex_to_int(char c) {
  if (isdigit(c))
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  
  return c - 'A' + 10;
}

// エスケープされた文字をそれに対応したASCIIコードで返す
static int read_escaped(char **new_pos, char *p) {
  // 8進数
  if ('0' <= *p && *p <= '7') {
    int c = *p++ - '0';
    if ('0' <= *p && *p <= '7') {
      c = c * 8 + *p++ - '0';
      if ('0' <= *p && *p <= '7')
        c = c * 8 + *p++ - '0';
    }

    *new_pos = p - 1;
    return c;
  }

  // 16進数
  if (*p == 'x') {
    int c = 0;
    while (isxdigit(*(++p)))
      c = c * 16 + hex_to_int(*p);

    *new_pos = p - 1;
    return c;
  }

  *new_pos = p;

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

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    println("  .data");
    println("  .globl %s", var->name);
    println("%s:", var->name);

    if (var->init_data) {
      for (char *p = var->init_data; *p; p++) {
        if (*p == '\\') {
          println("  .byte %d", read_escaped(&p, p + 1));
          continue;
        }

        println("  .byte %d", *p);
      }
      println("  .byte 0");
    } else {
      println("  .zero %d", var->ty->size);
    }
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    current_fn = fn;
    assign_lvar_offsets(fn);

    println("  .globl %s", fn->name);
    println("  .text");
    println("%s:", fn->name);

    // プロローグ
    println("  push rbp");
    println("  mov rbp, rsp");
    println("  sub rsp, %d", fn->stack_size);

    // レジスタに置かれた引数をスタックにコピーしておく
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      if (var->ty->size == 1)
        println("  mov [rbp-%d], %s", var->offset, argreg8[i++]);
      else
        println("  mov [rbp-%d], %s", var->offset, argreg64[i++]);
    }

    gen_stmt(fn->body);
    assert(depth == 0);

    // エピローグ
    println(".L.return.%s:", fn->name);
    println("  mov rsp, rbp");
    println("  pop rbp");
    println("  ret");
  }

}

void codegen(Obj *prog, FILE *out) {
  output_file = out;

  println(".intel_syntax noprefix");
  emit_data(prog);
  emit_text(prog);
}

