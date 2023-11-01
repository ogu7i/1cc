#include "1cc.h"

// 使ってるスタックの深さ。push/popごとに増減
static int depth;
// 関数呼び出し時に引数をセットするレジスタ群 
static char *argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *argreg64[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
// 現在処理している関数
static Obj *current_fn;

// ラベル用カウンタ
static int count(void) {
  static int i = 1;
  return i++;
}

static void gen_expr(Node *node);

static void push(void) {
  printf("  push rax\n");
  depth++;
}

static void pop(char *arg) {
  printf("  pop %s\n", arg);
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
      printf("  lea rax, [rbp-%d]\n", node->var->offset);
    else
      printf("  lea rax, %s[rip]\n", node->var->name);

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
    printf("  movsbq rax, [rax]\n");
  else
    printf("  mov rax, [rax]\n");
}

static void store(Type *ty) {
  pop("rdi");

  if (ty->size == 1)
    printf("  mov [rdi], al\n");
  else
    printf("  mov [rdi], rax\n");
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
    case ND_FUNCALL: {
      int nargs = 0;
      for (Node *arg = node->args; arg; arg = arg->next) {
        gen_expr(arg);
        push();
        nargs++;
      }

      for (int i = nargs - 1; i >= 0; i--)
        pop(argreg64[i]);

      printf("  mov rax, 0\n");

      if (depth % 2 == 0) {
        printf("  call %s\n", node->funcname);
      } else {
        printf("  sub rsp, 8\n");
        printf("  call %s\n", node->funcname);
        printf("  add rsp, 8\n");
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
    case ND_EQ:
      printf("  cmp rax, rdi\n");
      printf("  sete al\n");
      printf("  movzb rax, al\n");
      return;
    case ND_NE:
      printf("  cmp rax, rdi\n");
      printf("  setne al\n");
      printf("  movzb rax, al\n");
      return;
    case ND_LT:
      printf("  cmp rax, rdi\n");
      printf("  setl al\n");
      printf("  movzb rax, al\n");
      return;
    case ND_LE:
      printf("  cmp rax, rdi\n");
      printf("  setle al\n");
      printf("  movzb rax, al\n");
      return;
  }

  error_tok(node->tok, "不正な式です");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
    case ND_RETURN:
      gen_expr(node->lhs);
      printf("  jmp .L.return.%s\n", current_fn->name);
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
      printf("  cmp rax, 0\n");
      printf("  je .L.else.%d\n", c);

      gen_stmt(node->then);
      printf("  jmp .L.end.%d\n", c);

      printf(".L.else.%d:\n", c);
      if (node->els) {
        gen_stmt(node->els);
      }

      printf(".L.end.%d:\n", c);
      return;
    }
    case ND_WHILE: {
      int c = count();

      printf(".L.begin.%d:\n", c);
      gen_expr(node->cond);
      printf("  cmp rax, 0\n");
      printf("  je .L.end.%d\n", c);
      gen_stmt(node->then);
      printf("  jmp .L.begin.%d\n", c);
      printf(".L.end.%d:\n", c);
      return;
    }
    case ND_FOR: {
      int c = count();
      gen_stmt(node->init);
      printf(".L.begin.%d:\n", c);
      
      if (node->cond) {
        gen_expr(node->cond);
        printf("  cmp rax, 0\n");
        printf("  je .L.end.%d\n", c);
      }

      gen_stmt(node->then);
      
      if (node->inc)
        gen_expr(node->inc);

      printf("  jmp .L.begin.%d\n", c);
      printf(".L.end.%d:\n", c);
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

    printf("  .data\n");
    printf("  .globl %s\n", var->name);
    printf("%s:\n", var->name);

    if (var->init_data) {
      for (char *p = var->init_data; *p; p++) {
        if (*p == '\\') {
          printf("  .byte %d\n", read_escaped(&p, p + 1));
          continue;
        }

        printf("  .byte %d\n", *p);
      }
      printf("  .byte 0\n");
    } else {
      printf("  .zero %d\n", var->ty->size);
    }
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    current_fn = fn;
    assign_lvar_offsets(fn);

    printf("  .globl %s\n", fn->name);
    printf("  .text\n");
    printf("%s:\n", fn->name);

    // プロローグ
    printf("  push rbp\n");
    printf("  mov rbp, rsp\n");
    printf("  sub rsp, %d\n", fn->stack_size);

    // レジスタに置かれた引数をスタックにコピーしておく
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      if (var->ty->size == 1)
        printf("  mov [rbp-%d], %s\n", var->offset, argreg8[i++]);
      else
        printf("  mov [rbp-%d], %s\n", var->offset, argreg64[i++]);
    }

    gen_stmt(fn->body);
    assert(depth == 0);

    // エピローグ
    printf(".L.return.%s:\n", fn->name);
    printf("  mov rsp, rbp\n");
    printf("  pop rbp\n");
    printf("  ret\n");
  }

}

void codegen(Obj *prog) {
  printf(".intel_syntax noprefix\n");
  emit_data(prog);
  emit_text(prog);
}

