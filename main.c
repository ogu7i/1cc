#include "1cc.h"

// 入力文字列
static char *current_input;

// コード生成

int main(int argc, char **argv) {
  if (argc != 2)
    error("%s: 引数の個数が正しくありません\n", argv[0]);

  // トークナイズとパース
  Token *tok = tokenize_file(argv[1]);
  Obj *prog = parse(tok);
  codegen(prog);

  return 0;
}

