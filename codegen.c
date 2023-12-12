#include "1cc.h"

// 使ってるスタックの深さ。push/popごとに増減
static int depth;
// 関数呼び出し時に引数をセットするレジスタ群 
static char *argreg8[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *argreg16[] = {"di", "si", "dx", "cx", "r8w", "r9w"};
static char *argreg32[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
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
int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// 与えられたノードの絶対アドレスを計算する
// ノードがメモリ内になければエラー
static void gen_addr(Node *node) {
  switch (node->kind) {
    case ND_VAR:
      if (node->var->is_local)
        println("  lea rax, [rbp-%d]", node->var->offset);
      else
        println("  lea rax, %s[rip]", node->var->name);

      return;
    case ND_DEREF:
      gen_expr(node->lhs);
      return;
    case ND_COMMA:
      gen_expr(node->lhs);
      gen_addr(node->rhs);
      return;
    case ND_MEMBER:
      gen_addr(node->lhs);
      println("  add rax, %d", node->member->offset);
      return;
  }

  error_tok(node->tok, "左辺値ではありません");
}

static void load(Type *ty) {
  if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION)
    return;

  if (ty->size == 1)
    println("  movsbl eax, [rax]");
  else if (ty->size == 2)
    println("  movswl eax, [rax]");
  else if (ty->size == 4)
    println("  movsxd rax, [rax]");
  else
    println("  mov rax, [rax]");
}

static void store(Type *ty) {
  pop("rdi");

  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (int i = 0; i < ty->size; i++) {
      println("  mov r8b, [rax+%d]", i);
      println("  mov [rdi+%d], r8b", i);
    }

    return;
  }

  if (ty->size == 1)
    println("  mov [rdi], al");
  else if(ty->size == 2)
    println("  mov [rdi], ax");
  else if (ty->size == 4)
    println("  mov [rdi], eax");
  else
    println("  mov [rdi], rax");
}

static void cmp_zero(Type *ty) {
  if (is_integer(ty) && ty->size <= 4)
    println("  cmp eax, 0");
  else
    println("  cmp rax, 0");
}

enum { I8, I16, I32, I64 };

static int getTypeId(Type *ty) {
  switch (ty->kind) {
    case TY_CHAR:
      return I8;
    case TY_SHORT:
      return I16;
    case TY_INT:
      return I32;
  }

  return I64;
}

static char i32i8[] = "movsbl eax, al";
static char i32i16[] = "movswl eax, ax";
static char i32i64[] = "movsxd rax, eax";

// 型キャスト用のテーブル
static char *cast_table[][10] = {
  {NULL, NULL, NULL, i32i64},    // i8
  {i32i8, NULL, NULL, i32i64},   // i16
  {i32i8, i32i16, NULL, i32i64}, // i32
  {i32i8, i32i16, NULL, NULL},   // i64
};

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    println("  setne al");
    println("  movzx eax, al");
    return;
  }

  int t1 = getTypeId(from);
  int t2 = getTypeId(to);
  if (cast_table[t1][t2])
    println("  %s", cast_table[t1][t2]);
}

static void gen_expr(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);

  switch (node->kind) {
    case ND_NUM:
      println("  mov rax, %ld", node->val);
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
    case ND_MEMBER:
      gen_addr(node);
      load(node->ty);
      return;
    case ND_ASSIGN:
      gen_addr(node->lhs);
      push();
      gen_expr(node->rhs);
      store(node->ty);
      return;
    case ND_COMMA:
      gen_expr(node->lhs);
      gen_expr(node->rhs);
      return;
    case ND_STMT_EXPR:
      for (Node *n = node->body; n; n = n->next)
        gen_stmt(n);
      return;
    case ND_CAST:
      gen_expr(node->lhs);
      cast(node->lhs->ty, node->ty);
      return;
    case ND_COND: {
      int c = count();
      gen_expr(node->cond);
      println("  cmp rax, 0");
      println("  je .L.else.%d", c);
      gen_expr(node->then);
      println("  jmp .L.end.%d", c);
      println(".L.else.%d:", c);
      gen_expr(node->els);
      println(".L.end.%d:", c);
      return;
    } case ND_NOT:
      gen_expr(node->lhs);
      println("  cmp rax, 0");
      println("  sete al");
      println("  movzx rax, al");
      return;
    case ND_BITNOT:
      gen_expr(node->lhs);
      println("  not rax");
      return;
    case ND_LOGAND: {
      int c = count();
      gen_expr(node->lhs);
      println("  cmp rax, 0");
      println("  je .L.false.%d", c);
      gen_expr(node->rhs);
      println("  cmp rax, 0");
      println("  je .L.false.%d", c);
      println("  mov rax, 1");
      println("  jmp .L.end.%d", c);
      println(".L.false.%d:", c);
      println("  mov rax, 0");
      println(".L.end.%d:", c);
      return;
    }
    case ND_LOGOR: {
      int c = count();
      gen_expr(node->lhs);
      println("  cmp rax, 0");
      println("  jne .L.true.%d", c);
      gen_expr(node->rhs);
      println("  cmp rax, 0");
      println("  jne .L.true.%d", c);
      println("  mov rax, 0");
      println("  jmp .L.end.%d", c);
      println(".L.true.%d:", c);
      println("  mov rax, 1");
      println(".L.end.%d:", c);
      return;
    }
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

  char *ax, *di;
  if (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base) {
    ax = "rax";
    di = "rdi";
  } else {
    ax = "eax";
    di = "edi";
  }

  switch (node->kind) {
    case ND_ADD:
      println("  add %s, %s", ax, di);
      return;
    case ND_SUB:
      println("  sub %s, %s", ax, di);
      return;
    case ND_MUL:
      println("  imul %s, %s", ax, di);
      return;
    case ND_DIV:
    case ND_MOD:
      if (node->lhs->ty->size == 8)
        println("  cqo");
      else
        println("  cdq");
      println("  idiv %s", di);

      if (node->kind == ND_MOD)
        println("  mov rax, rdx");
      return;
    case ND_BITAND:
      println("  and rax, rdi");
      return;
    case ND_BITOR:
      println("  or rax, rdi");
      return;
    case ND_BITXOR:
      println("  xor rax, rdi");
      return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      println("  cmp %s, %s", ax, di);
      
      if (node->kind == ND_EQ)
        println("  sete al");
      else if(node->kind == ND_NE)
        println("  setne al");
      else if(node->kind == ND_LT)
        println("  setl al");
      else if(node->kind == ND_LE)
        println("  setle al");

      println("  movzb rax, al");
      return;
    case ND_SHL:
      println("  mov rcx, rdi");
      println("  shl %s, cl", ax);
      return;
    case ND_SHR:
      println("  mov rcx, rdi");
      println("  sar %s, cl", ax);
      return;
  }

  error_tok(node->tok, "不正な式です");
}

static void gen_stmt(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);

  switch (node->kind) {
    case ND_GOTO:
      println("  jmp %s", node->unique_label);
      return;
    case ND_LABEL:
      println("%s:", node->unique_label);
      gen_stmt(node->lhs);
      return;
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
      println("  je %s", node->brk_label);
      gen_stmt(node->then);
      println("%s:", node->cont_label);
      println("  jmp .L.begin.%d", c);
      println("%s:", node->brk_label);
      return;
    }
    case ND_FOR: {
      int c = count();
      gen_stmt(node->init);
      println(".L.begin.%d:", c);
      
      if (node->cond) {
        gen_expr(node->cond);
        println("  cmp rax, 0");
        println("  je %s", node->brk_label);
      }

      gen_stmt(node->then);
      println("%s:", node->cont_label);

      if (node->inc)
        gen_expr(node->inc);

      println("  jmp .L.begin.%d", c);
      println("%s:", node->brk_label);
      return;
    }
    case ND_SWITCH:
      gen_expr(node->cond);

      for (Node *n = node->case_next; n; n = n->case_next) {
        char *reg = (node->cond->ty->size == 8) ? "rax" : "eax";
        println("  cmp %s, %ld", reg, n->val);
        println("  je %s", n->label);
      }

      if (node->default_case)
        println("  jmp %s", node->default_case->label);

      println("  jmp %s", node->brk_label);
      gen_stmt(node->then);
      println("%s:", node->brk_label);
      return;
    case ND_CASE:
      println("%s:", node->label);
      gen_stmt(node->lhs);
      return;
  }

  error_tok(node->tok, "不正な文です");
}

static void assign_lvar_offsets(Obj *fn) {
  int offset = 0;
  for (Obj *var = fn->locals; var; var = var->next) {
    offset += var->ty->size;
    offset = align_to(offset, var->ty->align);
    var->offset = offset;
  }

  fn->stack_size = align_to(offset, 16);
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    println("  .data");
    println("  .globl %s", var->name);
    println("%s:", var->name);

    if (var->init_data) {
      for (int i = 0; i < var->ty->size; i++)
        println("  .byte %d", var->init_data[i]);
    } else {
      println("  .zero %d", var->ty->size);
    }
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;

    current_fn = fn;
    assign_lvar_offsets(fn);

    if (fn->is_static)
      println("  .local %s", fn->name);
    else
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
      else if (var->ty->size == 2)
        println("  mov [rbp-%d], %s", var->offset, argreg16[i++]);
      else if (var->ty->size == 4)
        println("  mov [rbp-%d], %s", var->offset, argreg32[i++]);
      else if (var->ty->size == 8)
        println("  mov [rbp-%d], %s", var->offset, argreg64[i++]);
      else
        unreachable();
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

