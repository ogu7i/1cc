#!/bin/bash

cat <<EOF | gcc -xc -c -o tmp2.o -
int ret3() { return 3; }
int ret5() { return 5; }
int add(int x, int y) { return x+y; }
int sub(int x, int y) { return x-y; }

int add6(int a, int b, int c, int d, int e, int f) {
  return a+b+c+d+e+f;
}
EOF

assert() {
  expected="$1"
  input="$2"

  ./1cc "$input" > tmp.s || exit
  gcc -o tmp tmp.s tmp2.o
  ./tmp
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

# 数値
assert 0 'int main(){ return 0; }'
assert 42 'int main(){ return 42; }'
# 四則演算
assert 21 'int main(){ return 5+20-4; }'
assert 41 'int main(){ return  12 + 34 - 5 ; }'
assert 47 'int main(){ return 5+6*7; }'
assert 15 'int main(){ return 5*(9-6); }'
assert 4 'int main(){ return (3+5)/2; }'
# 単項プラスとマイナス
assert 10 'int main(){ return -10+20; }'
assert 10 'int main(){ return - -10; }'
assert 10 'int main(){ return - - +10; }'
# 比較演算子
assert 0 'int main(){ return 0==1; }'
assert 1 'int main(){ return 42==42; }'
assert 1 'int main(){ return 0!=1; }'
assert 0 'int main(){ return 42!=42; }'

assert 1 'int main(){ return 0<1; }'
assert 0 'int main(){ return 1<1; }'
assert 0 'int main(){ return 2<1; }'
assert 1 'int main(){ return 0<=1; }'
assert 1 'int main(){ return 1<=1; }'
assert 0 'int main(){ return 2<=1; }'

assert 1 'int main(){ return 1>0; }'
assert 0 'int main(){ return 1>1; }'
assert 0 'int main(){ return 1>2; }'
assert 1 'int main(){ return 1>=0; }'
assert 1 'int main(){ return 1>=1; }'
assert 0 'int main(){ return 1>=2; }'

# 変数と代入
assert 3 'int main(){ int a=3; return a; }'
assert 8 'int main(){ int a=3; int z=5; return a+z; }'
assert 6 'int main(){ int a; int b; a=b=3; return a+b; }'
assert 3 'int main(){ int foo=3; return foo; }'
assert 8 'int main(){ int foo123=3; int bar=5; return foo123+bar; }'

# return
assert 1 'int main(){ return 1; 2; 3; }'
assert 2 'int main(){ 1; return 2; 3; }'
assert 3 'int main(){ 1; 2; return 3; }'

# ブロック
assert 3 'int main(){ {1; {2;} return 3;} }'
assert 5 'int main(){ ;;; return 5; }'

# if
assert 3 'int main(){ if (0) return 2; return 3; }'
assert 3 'int main(){ if (1-1) return 2; return 3; }'
assert 2 'int main(){ if (1) return 2; return 3; }'
assert 2 'int main(){ if (2-1) return 2; return 3; }'
assert 4 'int main(){ if (0) { 1; 2; return 3; } else { return 4; } }'
assert 3 'int main(){ if (1) { 1; 2; return 3; } else { return 4; } }'
assert 2 'int main(){ if (1) { if (2) return 2; else return 3; } return 4; }'
assert 3 'int main(){ if (1) { if (0) return 2; else return 3; } return 4; }'
assert 4 'int main(){ if (0) { if (0) return 2; else return 3; } return 4; }'

# while
assert 10 'int main(){ int i=0; while(i<10) { i=i+1; } return i; }'

# for
assert 55 'int main(){ int i=0; int j=0; for (i=0; i<=10; i=i+1) j=i+j; return j; }'
assert 3 'int main(){ for (;;) { return 3; } return 5; }'
assert 3 'int main(){ int i=0; for (;;) { return 3; } return 5; }'

# ポインタ
assert 3 'int main(){ int x=3; return *&x; }'
assert 3 'int main(){ int x=3; int *y=&x; int **z=&y; return **z; }'
assert 5 'int main(){ int x=3; int y=5; return *(&x+1); }'
assert 3 'int main(){ int x=3; int y=5; return *(&y-1); }'
assert 5 'int main(){ int x=3; int y=5; return *(&x-(-1)); }'
assert 5 'int main(){ int x=3; int *y=&x; *y=5; return x; }'
assert 7 'int main(){ int x=3; int y=5; *(&x+1)=7; return y; }'
assert 7 'int main(){ int x=3; int y=5; *(&y-2+1)=7; return x; }'
assert 5 'int main(){ int x=3; return (&x+2)-&x+3; }'
assert 2 'int main(){ int x=1; int y=3; int z=5; return &z - &x; }'

assert 8 'int main(){ int x, y; x=3; y=5; return x+y; }'
assert 8 'int main(){ int x=3, y=5; return x+y; }'

# 関数呼び出し
assert 3 'int main(){ return ret3(); }'
assert 5 'int main(){ return ret5(); }'
assert 8 'int main(){ return add(3, 5); }'
assert 2 'int main(){ return sub(5, 3); }'
assert 21 'int main(){ return add6(1,2,3,4,5,6); }'
assert 66 'int main(){ return add6(1,2,add6(3,4,5,6,7,8),9,10,11); }'
assert 136 'int main(){ return add6(1,2,add6(3,add6(4,5,6,7,8,9),10,11,12,13),14,15,16); }'

# 関数定義
assert 32 'int main() { return ret32(); } int ret32() { return 32; }'
assert 7 'int main() { return add2(3,4); } int add2(int x, int y) { return x+y; }'
assert 1 'int main() { return sub2(4,3); } int sub2(int x, int y) { return x-y; }'
assert 55 'int main() { return fib(9); } int fib(int x) { if (x<=1) return 1; return fib(x-1) + fib(x-2); }'

# 配列
assert 3 'int main() { int x[2]; int *y=&x; *y=3; return *x; }'

assert 3 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+2); }'

# 配列の配列
assert 0 'int main() { int x[2][3]; int *y=x; *y=0; return **x; }'
assert 1 'int main() { int x[2][3]; int *y=x; *(y+1)=1; return *(*x+1); }'
assert 2 'int main() { int x[2][3]; int *y=x; *(y+2)=2; return *(*x+2); }'
assert 3 'int main() { int x[2][3]; int *y=x; *(y+3)=3; return **(x+1); }'
assert 4 'int main() { int x[2][3]; int *y=x; *(y+4)=4; return *(*(x+1)+1); }'
assert 5 'int main() { int x[2][3]; int *y=x; *(y+5)=5; return *(*(x+1)+2); }'

# 配列の添字
assert 3 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+2); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+2); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; 2[x]=5; return *(x+2); }'

assert 0 'int main() { int x[2][3]; int *y=x; y[0]=0; return x[0][0]; }'
assert 1 'int main() { int x[2][3]; int *y=x; y[1]=1; return x[0][1]; }'
assert 2 'int main() { int x[2][3]; int *y=x; y[2]=2; return x[0][2]; }'
assert 3 'int main() { int x[2][3]; int *y=x; y[3]=3; return x[1][0]; }'
assert 4 'int main() { int x[2][3]; int *y=x; y[4]=4; return x[1][1]; }'
assert 5 'int main() { int x[2][3]; int *y=x; y[5]=5; return x[1][2]; }'

# sizeof
assert 8 'int main() { int x; return sizeof(x); }'
assert 8 'int main() { int x; return sizeof x; }'
assert 8 'int main() { int *x; return sizeof(x); }'
assert 32 'int main() { int x[4]; return sizeof(x); }'
assert 96 'int main() { int x[3][4]; return sizeof(x); }'
assert 32 'int main() { int x[3][4]; return sizeof(*x); }'
assert 8 'int main() { int x[3][4]; return sizeof(**x); }'
assert 9 'int main() { int x[3][4]; return sizeof(**x) + 1; }'
assert 9 'int main() { int x[3][4]; return sizeof **x + 1; }'
assert 8 'int main() { int x[3][4]; return sizeof(**x + 1); }'
assert 8 'int main() { int x=1; return sizeof(x=2); }'
assert 1 'int main() { int x=1; sizeof(x=2); return x; }'

# グローバル変数
assert 0 'int x; int main() { return x; }'
assert 3 'int x; int main() { x=3; return x; }'
assert 7 'int x; int y; int main() { x=3; y=4; return x+y; }'
assert 7 'int x, y; int main() { x=3; y=4; return x+y; }'
assert 0 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[0]; }'
assert 1 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[1]; }'
assert 2 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[2]; }'
assert 3 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[3]; }'

assert 8 'int x; int main() { return sizeof(x); }'
assert 32 'int x[4]; int main() { return sizeof(x); }'

# char
assert 1 'int main() { char x=1; return x; }'
assert 1 'int main() { char x=1; char y=2; return x; }'
assert 2 'int main() { char x=1; char y=2; return y; }'

assert 1 'int main() { char x; return sizeof(x); }'
assert 10 'int main() { char x[10]; return sizeof(x); }'
assert 1 'int main() { return sub_char(7, 3, 3); } int sub_char(char a, char b, char c) { return a-b-c; }'

assert 1 'int main() { char x = 257; return x; }'
assert 1 'int main() { return foo(257); } char foo(int x) { return x; }'
assert 1 'int main() { return foo(257); } int foo(char x) { return x; }'

echo OK

