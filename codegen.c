#include "1cc.h"

static int depth;

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
    printf("  lea rax, [rbp-%d]\n", node->var->offset);
    return;
  }

  error("左辺値ではありません");
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
    case ND_VAR:
      gen_addr(node);
      printf("  mov rax, [rax]\n");
      return;
    case ND_ASSIGN:
      gen_addr(node->lhs);
      push();
      gen_expr(node->rhs);
      pop("rdi");
      printf("  mov [rdi], rax\n");
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

  error("不正な式です");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
    case ND_RETURN:
      gen_expr(node->lhs);
      printf("  jmp .L.return\n");
      return;
    case ND_EXPR_STMT:
      gen_expr(node->lhs);
      return;
    case ND_BLOCK:
      for (Node *n = node->body; n; n = n->next)
        gen_stmt(n);
      return;
  }

  error("不正な文です");
}

static void assign_lvar_offsets(Function *prog) {
  int offset = 0;
  for (Obj *var = prog->locals; var; var = var->next) {
    offset += 8;
    var->offset = offset;
  }

  prog->stack_size = align_to(offset, 16);
}

void codegen(Function *prog) {
  assign_lvar_offsets(prog);

  printf(".intel_syntax noprefix\n");
  printf("  .globl main\n");
  printf("main:\n");

  // プロローグ
  printf("  push rbp\n");
  printf("  mov rbp, rsp\n");
  printf("  sub rsp, %d\n", prog->stack_size);

  gen_stmt(prog->body);
  assert(depth == 0);

  // エピローグ
  printf(".L.return:\n");
  printf("  mov rsp, rbp\n");
  printf("  pop rbp\n");
  printf("  ret\n");
}

