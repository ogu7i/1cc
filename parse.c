#include "1cc.h"

// あるスコープで扱える変数やtypedef、enumのリスト
typedef struct VarScope VarScope;
struct VarScope {
  VarScope *next;
  char *name;
  Obj *var;
  Type *type_def;
  Type *enum_ty;
  int enum_val;
};

// 構造体/共用体、enumのタグ名リスト
typedef struct TagScope TagScope;
struct TagScope {
  TagScope *next;
  char *name;
  Type *ty;
};

// スコープのリスト
typedef struct Scope Scope;
struct Scope {
  Scope *next;
  VarScope *vars;
  TagScope *tags;
};

// typedefやexternのような変数属性
typedef struct {
  bool is_typedef;
  bool is_static;
} VarAttr;

// 変数の初期化子を表す構造体
// 初期化子はint x[2][2] = {{1,2}, {3,4}}のようにネスト可能で
// この構造体は木構造である。
typedef struct Initializer Initializer;
struct Initializer {
  Initializer *next;
  Type *ty;
  Token *tok;

  // 合成型(配列や構造体)でなく初期化子があれば`expr`は初期化式を持つ
  Node *expr;

  // 合成型の初期化子であれば`children`はその子の初期化子をもつ
  Initializer **children;
};

// ローカル変数の初期化子
typedef struct InitDesg InitDesg;
struct InitDesg {
  InitDesg *next;
  int idx;
  Obj *var;
};

// パース中に作られたローカル変数はこのリストの中に
static Obj *locals;
static Obj *globals;

static Scope *scope = &(Scope){};

// 現在パース中の関数オブジェクト
static Obj *current_fn;

// 現在パース中の関数のすべてのgoto文とラベルのリスト
static Node *gotos;
static Node *labels;

// breakのジャンプ先
static char *brk_label;

// continueのジャンプ先
static char *cont_label;

// switch文をパース中ならそのノード、そうでないならNULL
static Node *current_switch;

static bool is_typename(Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *basety);
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static int64_t const_expr(Token **rest, Token *tok);
static Node *conditional(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor(Token **rest, Token *tok);
static Node *bitxor(Token **rest, Token *tok);
static Node *bitand(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(Node *lhs, Node *rhs, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety);

static void enter_scope(void) {
  Scope *sc = calloc(1, sizeof(Scope));
  sc->next = scope;
  scope = sc;
}

static void leave_scope(void) {
  scope = scope->next;
}

// 変数の検索
static VarScope *find_var(Token *tok) {
  for (Scope *sc = scope; sc; sc = sc->next)
    for (VarScope *vs = sc->vars; vs; vs = vs->next)
      if (equal(tok, vs->name))
        return vs;

  return NULL;
}

// 構造体タグの検索
static Type *find_tag(Token *tok) {
  for (Scope *sc = scope; sc; sc = sc->next)
    for (TagScope *ts = sc->tags; ts; ts = ts->next)
      if (equal(tok, ts->name))
        return ts->ty;

  return NULL;
}

// typedefの検索
static Type *find_typedef(Token *tok) {
  if (tok->kind == TK_IDENT) {
    VarScope *sc = find_var(tok);
    if (sc)
      return sc->type_def;
  }
  return NULL;
}

// 現在のスコープに新しいタグを追加する
static void push_tag_scope(Token *tok, Type *ty) {
  TagScope *sc = calloc(1, sizeof(TagScope));
  sc->name = strndup(tok->loc, tok->len);
  sc->ty = ty;
  sc->next = scope->tags;
  scope->tags = sc;
}

// 新しいノードを作る。種類をセットするだけ。
static Node *new_node(NodeKind kind, Token *tok) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->tok = tok;
  return node;
}

// 新しい2分木ノードを作る。
static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

// 新しい単項ノードを作る。
static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = expr;
  return node;
}

// 新しい数値ノードを作る。
static Node *new_num(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  return node;
}

static Node *new_long(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->ty = ty_long;
  return node;
}

// 新しい変数ノードを作る
static Node *new_var_node(Obj *var, Token *tok) {
  Node *node = new_node(ND_VAR, tok);
  node->var = var;
  return node;
}

// 新しいキャストノードを作る
Node *new_cast(Node *expr, Type *ty) {
  add_type(expr);

  Node *node = new_node(ND_CAST, expr->tok);
  node->lhs = expr;
  node->ty = copy_type(ty);
  return node;
}

// 現在のスコープに指定した名前を加える
static VarScope *push_scope(char *name) {
  VarScope *sc = calloc(1, sizeof(VarScope));
  sc->name = name;
  sc->next = scope->vars;
  scope->vars = sc;
  return sc;
}

static Initializer *new_initializer(Type *ty) {
  Initializer *init = calloc(1, sizeof(Initializer));
  init->ty = ty;

  if (ty->kind == TY_ARRAY) {
    init->children = calloc(ty->array_len, sizeof(Initializer *));
    for (int i = 0; i < ty->array_len; i++)
      init->children[i] = new_initializer(ty->base);
  }

  return init;
}

static Obj *new_var(char *name, Type *ty) {
  Obj *var = calloc(1, sizeof(Obj));
  var->name = name;
  var->ty = ty;
  VarScope *sc = push_scope(name);
  sc->var = var;
  return var;
}

// ty型のローカル変数を作る
static Obj *new_lvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->is_local = true;
  var->next = locals;
  locals = var;
  return var;
}

// ty型のグローバル変数を作る
static Obj *new_gvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->next = globals;
  globals = var;
  return var;
}

static char *new_unique_name(void) {
  static int id = 0;
  char *label = calloc(1, 20);
  sprintf(label, ".L..%d", id++);
  return label;
}

static Obj *new_anon_gvar(Type *ty) {
  return new_gvar(new_unique_name(), ty);
}

// 新しい文字列リテラルを作る
static Obj *new_string_literal(char *p, Type *ty) {
  Obj *var = new_anon_gvar(ty);
  var->init_data = p;
  return var;
}

static char *get_ident(Token *tok) {
  if (tok->kind != TK_IDENT)
    error_tok(tok, "識別子ではありません");

  return strndup(tok->loc, tok->len);
}

// stmt = "return" expr ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "switch" "(" expr ")" stmt
//      | "case" const-expr ":" stmt
//      | "default" ":" stmt
//      | "while" "(" expr ")" stmt
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "goto" ident ";"
//      | "break" ";"
//      | "continue" ";"
//      | ident ":" stmt
//      | "{" compound-stmt 
//      | expr-stmt
static Node *stmt(Token **rest, Token *tok) {
  if (equal(tok, "return")) {
    Node *node = new_node(ND_RETURN, tok);
    Node *exp = expr(&tok, tok->next);
    *rest = skip(tok, ";");

    add_type(exp);
    node->lhs = new_cast(exp, current_fn->ty->return_ty);
    return node;
  }

  if (equal(tok, "if")) {
    tok = skip(tok->next, "(");
    Node *node = new_node(ND_IF, tok);
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(&tok, tok);

    if (equal(tok, "else")) {
      node->els = stmt(&tok, tok->next);
    }
    *rest = tok;
    return node;
  }

  if (equal(tok, "switch")) {
    Node *node = new_node(ND_SWITCH, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");

    Node *sw = current_switch;
    current_switch = node;

    char *brk = brk_label;
    brk_label = node->brk_label = new_unique_name();

    node->then = stmt(rest, tok);

    current_switch = sw;
    brk_label = brk;
    return node;
  }

  if (equal(tok, "case")) {
    if (!current_switch)
      error_tok(tok, "switch内にありません");

    Node *node = new_node(ND_CASE, tok);
    int val = const_expr(&tok, tok->next); 
    tok = skip(tok, ":");
    node->label = new_unique_name();
    node->lhs = stmt(rest, tok);
    node->val = val;
    node->case_next = current_switch->case_next;
    current_switch->case_next = node;
    return node;
  }

  if (equal(tok, "default")) {
    if (!current_switch)
      error_tok(tok, "switch内にありません");

    Node *node = new_node(ND_CASE, tok);
    tok = skip(tok->next, ":");
    node->label = new_unique_name();
    node->lhs = stmt(rest, tok);
    current_switch->default_case = node;
    return node;
  }

  if (equal(tok, "while")) {
    tok = skip(tok->next, "(");
    Node *node = new_node(ND_WHILE, tok);
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");

    char *brk = brk_label;
    char *cont = cont_label;
    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();

    node->then = stmt(rest, tok);

    brk_label = brk;
    cont_label = cont;
    return node;
  }

  if (equal(tok, "for")) {
    tok = skip(tok->next, "(");
    Node *node = new_node(ND_FOR, tok);

    enter_scope();

    char *brk = brk_label;
    char *cont = cont_label;

    brk_label = node->brk_label = new_unique_name();
    cont_label = node->cont_label = new_unique_name();

    if (is_typename(tok)) {
      Type *basety = declspec(&tok, tok, NULL);
      node->init = declaration(&tok, tok, basety);
    } else {
      node->init = expr_stmt(&tok, tok);
    }

    if (!equal(tok, ";"))
      node->cond = expr(&tok, tok);
    tok = skip(tok, ";");

    if (!equal(tok, ")"))
      node->inc = expr(&tok, tok);
    tok = skip(tok, ")");

    node->then = stmt(rest, tok);
    leave_scope();
    brk_label = brk;
    cont_label = cont;
    return node;
  }

  if (equal(tok, "goto")) {
    Node *node = new_node(ND_GOTO, tok);
    node->label = get_ident(tok->next);
    node->goto_next = gotos;
    gotos = node;
    *rest = skip(tok->next->next, ";");
    return node;
  }

  if (equal(tok, "break")) {
    if (!brk_label)
      error_tok(tok, "breakがループ内やswitch内にありません");

    Node *node = new_node(ND_GOTO, tok);
    node->unique_label = brk_label;
    *rest = skip(tok->next, ";");
    return node;
  }

  if (equal(tok, "continue")) {
    if (!cont_label)
      error_tok(tok, "continueがループ内にありません");

    Node *node = new_node(ND_GOTO, tok);
    node->unique_label = cont_label;
    *rest = skip(tok->next, ";");
    return node;
  }

  if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
    Node *node = new_node(ND_LABEL, tok);
    node->label = strndup(tok->loc, tok->len);
    node->unique_label = new_unique_name();
    node->lhs = stmt(rest, tok->next->next);
    node->goto_next = labels;
    labels = node;
    return node;
  }

  if (equal(tok, "{"))
    return compound_stmt(rest, tok->next);

  return expr_stmt(rest, tok);
}

// gotoとラベルをマッチさせる
//
// ラベルが後から出現できるのでgotoは関数パースと同時に
// 解決できない。
// なので、関数全体をパースしてからやらないといけない
static void resolve_goto_labels(void) {
  for (Node *x = gotos; x; x = x->goto_next) {
    for (Node *y = labels; y; y = y->goto_next) {
      if (!strcmp(x->label, y->label)) {
        x->unique_label = y->unique_label;
        break;
      }
    }

    if (x->unique_label == NULL)
      error_tok(x->tok->next, "未宣言のラベルです");
  }

  gotos = labels = NULL;
}

// struct-members = (declspec declarator ("," declarator)* ";")* "}"
static void struct_members(Token **rest, Token *tok, Type *ty) {
  Member head = {};
  Member *cur = &head;

  while (!equal(tok, "}")) {
    Type *base_ty = declspec(&tok, tok, NULL);
    int i = 0;

    while (!consume(&tok, tok, ";")) {
      if (i++)
        tok = skip(tok, ",");

      Member *mem = calloc(1, sizeof(Member));
      mem->ty = declarator(&tok, tok, base_ty);
      mem->name = mem->ty->name;
      cur = cur->next = mem;
    }
  }

  *rest = tok->next;
  ty->members = head.next;
}

// struct-union-decl = ident? ("{" struct-members)?
static Type *struct_union_decl(Token **rest, Token *tok) {
  // タグを読んで保管しておく
  Token *tag = NULL;
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }

  if (tag && !equal(tok, "{")) {
    *rest = tok;

    Type *ty = find_tag(tag);
    if (ty)
      return ty;

    ty = struct_type();
    // このサイズが-1のままなら、未定義の構造体型である
    ty->size = -1;
    push_tag_scope(tag, ty);
    return ty;
  }

  tok = skip(tok, "{");

  Type *ty = struct_type();
  struct_members(rest, tok, ty);

  if (tag) {
    // 再定義なら、以前の型を上書きする。
    // そうでないなら、構造体型として登録。
    for (TagScope *sc = scope->tags; sc; sc = sc->next) {
      if (equal(tag, sc->name)) {
        *sc->ty = *ty;
        return sc->ty;
      }
    }

    push_tag_scope(tag, ty);
  }

  return ty;
}

// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  ty->kind = TY_STRUCT;

  if (ty->size < 0)
    return ty;

  // メンバのオフセットを割り当てる
  int offset = 0;
  for (Member *mem = ty->members; mem; mem = mem->next) {
    offset = align_to(offset, mem->ty->align);
    mem->offset = offset;
    offset += mem->ty->size;

    if (ty->align < mem->ty->align)
      ty->align = mem->ty->align;
  }
  ty->size = align_to(offset, ty->align);

  return ty;
}

// union-decl = struct-union-decl
static Type *union_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  ty->kind = TY_UNION;

  if (ty->size < 0)
    return ty;

  // 共用体の場合はオフセットの割当は不要
  // 各メンバはcallocで0初期化されてるのでオフセットも0
  // メンバ中の最大のアライメントとサイズを共用体のものとして
  // セットしておけば良い
  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (ty->align < mem->ty->align)
      ty->align = mem->ty->align;
    if (ty->size < mem->ty->size)
      ty->size = mem->ty->size;
  }
  ty->size = align_to(ty->size, ty->align);
  return ty;
}

static Member *get_struct_member(Type *ty, Token *tok) {
  for (Member *mem = ty->members; mem; mem = mem->next)
    if (mem->name->len == tok->len && !strncmp(mem->name->loc, tok->loc, tok->len))
      return mem;

  error_tok(tok, "そのようなメンバはありません");
}

static Node *struct_ref(Node *lhs, Token *tok) {
  add_type(lhs);
  if (lhs->ty->kind != TY_STRUCT && lhs->ty->kind != TY_UNION)
    error_tok(lhs->tok, "構造体でも共用体でもありません");

  Node *node = new_unary(ND_MEMBER, lhs, tok);
  node->member = get_struct_member(lhs->ty, tok);
  return node;
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long" 
//          | "typedef" | "static"
//          | "struct" struct-decl | "union" union-decl
//          | "enum" enum-specifier)+
//
// 型指定子はどういう順番で出現しても良い。`long int`でも`int long`でも同じ。
// ただし、char intみたいなのは認められない。
// ここではビットマップを使って、出現回数を取得して型を決める。
// 組み合わせにない場合はエラーを表示して終了。
static Type *declspec(Token **rest, Token *tok, VarAttr *attr) {
  enum {
    VOID =  1 << 0,
    BOOL =  1 << 2,
    CHAR =  1 << 4,
    SHORT = 1 << 6,
    INT =   1 << 8,
    LONG =  1 << 10,
    OTHER = 1 << 12,
  };

  Type *ty = ty_int;
  int counter = 0;

  while (is_typename(tok)) {
    if (equal(tok, "typedef") || equal(tok, "static")) {
      if (!attr)
        error_tok(tok, "記憶クラス指定子はこのコンテキストで許可されていません");

      if (equal(tok, "typedef"))
        attr->is_typedef = true;
      else
        attr->is_static = true;

      if (attr->is_typedef && attr->is_static)
        error_tok(tok, "typedefとstaticを同時に使うことはできません");

      tok = tok->next;
      continue;
    }

    Type *ty2 = find_typedef(tok);
    if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") || ty2) {
      if (counter)
        break;

      if (equal(tok, "struct"))
        ty = struct_decl(&tok, tok->next);
      else if (equal(tok, "union"))
        ty = union_decl(&tok, tok->next);
      else if (equal(tok, "enum"))
        ty = enum_specifier(&tok, tok->next);
      else {
        ty = ty2;
        tok = tok->next;
      }

      counter += OTHER;
      continue;
    }

    if (equal(tok, "void"))
      counter += VOID;
    else if (equal(tok, "_Bool"))
      counter += BOOL;
    else if (equal(tok, "char"))
      counter += CHAR;
    else if (equal(tok, "short"))
      counter += SHORT;
    else if (equal(tok, "int"))
      counter += INT;
    else if (equal(tok, "long"))
      counter += LONG;
    else
      unreachable();

    switch (counter) {
      case VOID:
        ty = ty_void;
        break;
      case BOOL:
        ty = ty_bool;
        break;
      case CHAR:
        ty = ty_char;
        break;
      case SHORT:
      case SHORT + INT:
        ty = ty_short;
        break;
      case INT:
        ty = ty_int;
        break;
      case LONG:
      case LONG + INT:
      case LONG + LONG:
      case LONG + LONG + INT:
        ty = ty_long;
        break;
      default:
        error_tok(tok, "不正な型です");
    }

    tok = tok->next;
  }

  *rest = tok;
  return ty;
}

// enum-specifier = ident? "{" enum-list? "}"
//                | ident ("{" enum-list? "}")?
//
// enum-list      = ident ("=" const_expr)? ("," ident ("=" const_expr)?)*
static Type *enum_specifier(Token **rest, Token *tok) {
  Type *ty = enum_type();

  // タグの読み込み
  Token *tag = NULL;
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }

  if (tag && !equal(tok, "{")) {
    Type *ty = find_tag(tag);
    if (!ty)
      error_tok(tag, "不明な列挙型です");
    if (ty->kind != TY_ENUM)
      error_tok(tag, "enumタグではありません");
    
    *rest = tok;
    return ty;
  }

  tok = skip(tok, "{");

  int i = 0;
  int val = 0;
  while (!equal(tok, "}")) {
    if (i++ > 0)
      tok = skip(tok, ",");

    char *name = get_ident(tok);
    tok = tok->next;

    if (equal(tok, "="))
      val = const_expr(&tok, tok->next);

    VarScope *sc = push_scope(name);
    sc->enum_ty = ty;
    sc->enum_val = val++;
  }

  *rest = tok->next;

  if (tag)
    push_tag_scope(tag, ty);
  return ty;
}

// func-params = declspec declarator ("," declspec declarator)*
static Type *func_params(Token **rest, Token *tok, Type *ty) {
  Type head = {};
  Type *cur = &head;
  while (!equal(tok, ")")) {
    if (cur != &head)
      tok = skip(tok, ",");

    /*
    Type *base_ty = declspec(&tok, tok, NULL);
    Type *param_ty = declarator(&tok, tok, base_ty);
    cur = cur->next = copy_type(param_ty);
    */
    Type *param_ty = declspec(&tok, tok, NULL);
    param_ty = declarator(&tok, tok, param_ty);

    // 関数の引数の場合だけ、配列はポインタに変換される
    if (param_ty->kind == TY_ARRAY) {
      Token *name = param_ty->name;
      param_ty = pointer_to(param_ty->base);
      param_ty->name = name;
    }

    cur = cur->next = copy_type(param_ty);
  }

  ty = func_type(ty);
  ty->params = head.next;
  *rest = tok->next;
  return ty;
}

// array-dimensions = const-expr? "]" type-suffix
static Type *array_dimensions(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "]")) {
    ty = type_suffix(rest, tok->next, ty);
    return array_of(ty, -1);
  }

  int sz = const_expr(&tok, tok);
  tok = skip(tok, "]");
  ty = type_suffix(rest, tok, ty);
  return array_of(ty, sz);
}

// type-suffix = "(" func-params? ")" | "[" array-dimensions | ε
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "("))
    return func_params(rest, tok->next, ty);

  if (equal(tok, "["))
    return array_dimensions(rest, tok->next, ty);

  *rest = tok;
  return ty;
}

// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) type-suffix
static Type *declarator(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*"))
    ty = pointer_to(ty);

  if (equal(tok, "(")) {
    Token *start = tok;
    Type dummy = {};
    declarator(&tok, start->next, &dummy);
    tok = skip(tok, ")");
    ty = type_suffix(rest, tok, ty);
    return declarator(&tok, start->next, ty);
  }

  if (tok->kind != TK_IDENT)
    error_tok(tok, "変数名ではありません");

  ty = type_suffix(rest, tok->next, ty);
  ty->name = tok;
  return ty;
}

// abstract-declarator = "*"* ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*"))
    ty = pointer_to(ty);

  if (equal(tok, "(")) {
    Token *start = tok;
    Type dummy = {};
    abstract_declarator(&tok, start->next, &dummy);
    tok = skip(tok, ")");
    ty = type_suffix(rest, tok, ty);
    return abstract_declarator(&tok, start->next, ty);
  }

  return type_suffix(rest, tok, ty);
}

// type-name = declspec abstract-declarator
static Type *typename(Token **rest, Token *tok) {
  Type *ty = declspec(&tok, tok, NULL);
  return abstract_declarator(rest, tok, ty);
}

// declaration = declspec (declarator ("=" assign)? ("," declarator ("=" assign)?)*)? ";"
static Node *declaration(Token **rest, Token *tok, Type *basety) {
  Node head = {};
  Node *cur = &head;
  bool first = true;
  while (!equal(tok, ";")) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    Type *ty = declarator(&tok, tok, basety);
    if (ty->size < 0)
      error_tok(tok, "変数は不完全型です");
    if (ty->kind == TY_VOID)
      error_tok(tok, "void型の変数は宣言できません");

    Obj *var = new_lvar(get_ident(ty->name), ty);

    if (equal(tok, "=")) {
      Node *expr = lvar_initializer(&tok, tok->next, var);
      cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
    }
  }

  Node *node = new_node(ND_BLOCK, tok);
  node->body = head.next;
  *rest = skip(tok, ";");
  return node;
}

static Token *skip_excess_element(Token *tok) {
  if (equal(tok, "{")) {
    tok = skip_excess_element(tok->next);
    return skip(tok, "}");
  }

  assign(&tok, tok);
  return tok;
}

// initializer = "{" initializer ("," initializer)* "}" | assign
static void initializer2(Token **rest, Token *tok, Initializer *init) {
  if (init->ty->kind == TY_ARRAY) {
    tok = skip(tok, "{");

    for (int i = 0; !consume(rest, tok, "}"); i++) {
      if (i > 0)
        tok = skip(tok, ",");

      if (i < init->ty->array_len)
        initializer2(&tok, tok, init->children[i]);
      else
        tok = skip_excess_element(tok);
    }
    return;
  }

  init->expr = assign(rest, tok);
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty) {
  Initializer *init = new_initializer(ty);
  initializer2(rest, tok, init);
  return init;
}

static Node *init_desg_expr(InitDesg *desg, Token *tok) {
  if (desg->var)
    return new_var_node(desg->var, tok);

  Node *lhs = init_desg_expr(desg->next, tok);
  Node *rhs = new_num(desg->idx, tok);
  return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
  if (ty->kind == TY_ARRAY) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    for (int i = 0; i < ty->array_len; i++) {
      InitDesg desg2 = { desg, i };
      Node *rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (!init->expr)
    return new_node(ND_NULL_EXPR, tok);

  Node *lhs = init_desg_expr(desg, tok);
  Node *rhs = init->expr;
  return new_binary(ND_ASSIGN, lhs, rhs, tok);
}

// 初期化子を持った変数定義は変数定義と代入の省略記法。
// この関数は初期化子のための代入式を生成する。
// 例えばint x[2][2] = {{6, 7}, {8, 9}}は
// x[0][0] = 6;
// x[0][1] = 7;
// x[1][0] = 8;
// x[1][1] = 9;
// に変換される。
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var) {
  Initializer *init = initializer(rest, tok, var->ty);
  InitDesg desg = { NULL, 0, var };

  // 初期化子で指定されてない要素は0初期化する。
  // ここでは、簡単のために変数のメモリ領域全体をあらかじめ
  // 0初期化しておく。
  Node *lhs = new_node(ND_MEMZERO, tok);
  lhs->var = var;

  Node *rhs = create_lvar_init(init, var->ty, &desg, tok);
  return new_binary(ND_COMMA, lhs, rhs, tok);
}

static bool is_typename(Token *tok) {
  static char *kw[] = {
    "void", "_Bool", "char", "short", "int", "long", "struct", 
    "union", "typedef", "enum", "static",
  };

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
    if (equal(tok, kw[i]))
      return true;

  return find_typedef(tok);
}

// compound-stmt = (typedef | declaration | stmt)* "}"
static Node *compound_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_BLOCK, tok);

  enter_scope();

  Node head = {};
  Node *cur = &head;
  while (!equal(tok, "}")) {
    if (is_typename(tok) && !equal(tok->next, ":")) {
      VarAttr attr = {};
      Type *basety = declspec(&tok, tok, &attr);

      if (attr.is_typedef) {
        tok = parse_typedef(tok, basety);
        continue;
      }

      cur = cur->next = declaration(&tok, tok, basety);
    } else {
      cur = cur->next = stmt(&tok, tok);
    }

    add_type(cur);
  }

  leave_scope();

  node->body = head.next;
  *rest = tok->next;
  return node;
}

// expr-stmt = expr? ";"
static Node *expr_stmt(Token **rest, Token *tok) {
  if (equal(tok, ";")) {
    *rest = tok->next;
    return new_node(ND_BLOCK, tok);
  }

  Node *node = new_node(ND_EXPR_STMT, tok);
  node->lhs = expr(&tok, tok);
  *rest = skip(tok, ";");
  return node;
}

// expr = assign ("," expr)?
static Node *expr(Token **rest, Token *tok) {
  Node *node = assign(&tok, tok);

  if (equal(tok, ","))
    return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);

  *rest = tok;
  return node;
}

// 与えられたノードを定数式として評価する
static int64_t eval(Node *node) {
  add_type(node);

  switch (node->kind) {
    case ND_ADD:
      return eval(node->lhs) + eval(node->rhs);
    case ND_SUB:
      return eval(node->lhs) - eval(node->rhs);
    case ND_MUL:
      return eval(node->lhs) * eval(node->rhs);
    case ND_DIV:
      return eval(node->lhs) / eval(node->rhs);
    case ND_NEG:
      return -eval(node->lhs);
    case ND_MOD:
      return eval(node->lhs) % eval(node->rhs);
    case ND_BITAND:
      return eval(node->lhs) & eval(node->rhs);
    case ND_BITOR:
      return eval(node->lhs) | eval(node->rhs);
    case ND_BITXOR:
      return eval(node->lhs) ^ eval(node->rhs);
    case ND_SHL:
      return eval(node->lhs) << eval(node->rhs);
    case ND_SHR:
      return eval(node->lhs) >> eval(node->rhs);
    case ND_EQ:
      return eval(node->lhs) == eval(node->rhs);
    case ND_NE:
      return eval(node->lhs) != eval(node->rhs);
    case ND_LT:
      return eval(node->lhs) < eval(node->rhs);
    case ND_LE:
      return eval(node->lhs) <= eval(node->rhs);
    case ND_COND:
      return eval(node->cond) ? eval(node->then) : eval(node->els);
    case ND_COMMA:
      return eval(node->rhs);
    case ND_NOT:
      return !eval(node->lhs);
    case ND_BITNOT:
      return ~eval(node->lhs);
    case ND_LOGAND:
      return eval(node->lhs) && eval(node->rhs);
    case ND_LOGOR:
      return eval(node->lhs) || eval(node->rhs);
    case ND_CAST:
      if (is_integer(node->ty)) {
        switch (node->ty->size) {
          case 1: return (uint8_t)eval(node->lhs);
          case 2: return (uint16_t)eval(node->lhs);
          case 4: return (uint32_t)eval(node->lhs);
        }
      }
      return eval(node->lhs);
    case ND_NUM:
      return node->val;
  }

  error_tok(node->tok, "定数式ではありません");
}

static int64_t const_expr(Token **rest, Token *tok) {
  Node *node = conditional(rest, tok);
  return eval(node);
}

// `A op= B`は`tmp = &A, *tmp = *tmp op B;に変換する。
// これは単純にA = A op BとしてしまうとAが2回評価されるからである。
static Node *to_assign(Node *binary) {
  add_type(binary->lhs);
  add_type(binary->rhs);
  Token *tok = binary->tok;

  Obj *var = new_lvar("", pointer_to(binary->lhs->ty));

  Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                           new_unary(ND_ADDR, binary->lhs, tok),
                           tok);

  Node *expr2 = 
    new_binary(ND_ASSIGN,
               new_unary(ND_DEREF, new_var_node(var, tok), tok),
               new_binary(binary->kind,
                          new_unary(ND_DEREF, new_var_node(var, tok), tok),
                          binary->rhs,
                          tok),
               tok);

  return new_binary(ND_COMMA, expr1, expr2, tok);
}

// assign = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&="
//           | "|=", | "^=" | "<<=" | ">>="
static Node *assign(Token **rest, Token *tok) {
  Node *node = conditional(&tok, tok);

  if (equal(tok, "="))
    return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);

  if (equal(tok, "+="))
    return to_assign(new_add(node, assign(rest, tok->next), tok));

  if (equal(tok, "-="))
    return to_assign(new_sub(node, assign(rest, tok->next), tok));

  if (equal(tok, "*="))
    return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));

  if (equal(tok, "/="))
    return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));
    
  if (equal(tok, "%="))
    return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));

  if (equal(tok, "&="))
    return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));

  if (equal(tok, "|="))
    return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));

  if (equal(tok, "^="))
    return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));

  if (equal(tok, "<<="))
    return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));
  
  if (equal(tok, ">>="))
    return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));

  *rest = tok;
  return node;
}

// conditional = logor ("?" expr ":" conditional)?
static Node *conditional(Token **rest, Token *tok) {
  Node *cond = logor(&tok, tok);

  if (!equal(tok, "?")) {
    *rest = tok;
    return cond;
  }

  Node *node = new_node(ND_COND, tok);
  node->cond = cond;
  node->then = expr(&tok, tok->next);
  tok = skip(tok, ":");
  node->els = conditional(rest, tok);
  return node;
}

// logor = logand("||" logand)*
static Node *logor(Token **rest, Token *tok) {
  Node *node = logand(&tok, tok);
  while (equal(tok, "||")) {
    Token *start = tok;
    node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
  }

  *rest = tok;
  return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(Token **rest, Token *tok) {
  Node *node = bitor(&tok, tok);
  while (equal(tok, "&&")) {
    Token *start = tok;
    node = new_binary(ND_LOGAND, node, bitor(&tok, tok->next), start);
  }

  *rest = tok;
  return node;
}

// bitor = bitxor ("|" bitxor)*
static Node *bitor(Token **rest, Token *tok) {
  Node *node = bitxor(&tok, tok);
  while (equal(tok, "|")) {
    Token *start = tok;
    node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
  }

  *rest = tok;
  return node;
}

// bitxor = bitand ("^" bitand)*
static Node *bitxor(Token **rest, Token *tok) {
  Node *node = bitand(&tok, tok);
  while (equal(tok, "^")) {
    Token *start = tok;
    node = new_binary(ND_BITXOR, node, bitand(&tok, tok->next), start);
  }

  *rest = tok;
  return node;
}

// bitand = equality ("&" equality)*
static Node *bitand(Token **rest, Token *tok) {
  Node *node = equality(&tok, tok);
  while (equal(tok, "&")) {
    Token *start = tok;
    node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
  }

  *rest = tok;
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **rest, Token *tok) {
  Node *node = relational(&tok, tok);

  for (;;) {
    Token *start = tok;
    if (equal(tok, "==")) {
      node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "!=")) {
      node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">= shift)*
static Node *relational(Token **rest, Token *tok) {
  Node *node = shift(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "<")) {
      node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "<=")) {
      node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, ">")) {
      node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
      continue;
    }

    if (equal(tok, ">=")) {
      node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **rest, Token *tok) {
  Node *node = add(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "<<")) {
      node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, ">>")) {
      node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_binary(ND_ADD, lhs, rhs, tok);

  // ptr + ptr
  if (lhs->ty->base && rhs->ty->base)
    error_tok(tok, "ポインタ同士の加算はできません");

  // num + ptr (ptr + numに変換しておく)
  if (is_integer(lhs->ty) && rhs->ty->base) {
    Node *tmp = lhs;
    lhs = rhs;
    rhs = tmp;
  }

  // ptr + num
  rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
  return new_binary(ND_ADD, lhs, rhs, tok);
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num - num
  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_binary(ND_SUB, lhs, rhs, tok);

  // ptr - num
  if (lhs->ty->base && is_integer(rhs->ty)) {
    rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
    return new_binary(ND_SUB, lhs, rhs, tok);
  }

  // ptr - ptr 要素数
  if (lhs->ty->base && rhs->ty->base) {
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);
    node->ty = ty_long;
    return new_binary(ND_DIV, node, new_num(lhs->ty->base->size, tok), tok);
  }

  error_tok(tok, "不正なオペランドです");
}

// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
  Node *node = mul(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "+")) {
      node = new_add(node, mul(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "-")) {
      node = new_sub(node, mul(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **rest, Token *tok) {
  Node *node = cast(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "*")) {
      node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "/")) {
      node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "%")) {
      node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// cast = "(" type-name ")" cast | unary
static Node *cast(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    Token *start = tok;
    Type *ty = typename(&tok, tok->next);
    tok = skip(tok, ")");
    Node *node = new_cast(cast(rest, tok), ty);
    node->tok = start;
    return node;
  }

  return unary(rest, tok);
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | postfix
//       | "sizeof" "(" type-name ")"
//       | "sizeof" cast
static Node *unary(Token **rest, Token *tok) {
  if (equal(tok, "+"))
    return cast(rest, tok->next);

  if (equal(tok, "-"))
    return new_unary(ND_NEG, cast(rest, tok->next), tok);

  if (equal(tok, "*"))
    return new_unary(ND_DEREF, cast(rest, tok->next), tok);

  if (equal(tok, "&"))
    return new_unary(ND_ADDR, cast(rest, tok->next), tok);

  if (equal(tok, "!"))
    return new_unary(ND_NOT, cast(rest, tok->next), tok);

  if (equal(tok, "~"))
    return new_unary(ND_BITNOT, cast(rest, tok->next), tok);

  // `++i`は`i += 1`とする
  if (equal(tok, "++"))
    return to_assign(new_add(unary(rest, tok->next), new_num(1, tok), tok));

  // `--i`も`i -= 1`とする
  if (equal(tok, "--"))
    return to_assign(new_sub(unary(rest, tok->next), new_num(1, tok), tok));

  if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
    Token *start = tok;
    Type *ty = typename(&tok, tok->next->next);
    *rest = skip(tok, ")");
    return new_num(ty->size, start);
  }

  if (equal(tok, "sizeof")) {
    Node *n = unary(rest, tok->next);
    add_type(n);
    return new_num(n->ty->size, tok);
  }

  return postfix(rest, tok);
}

// funcall = ident "(" (assign ("," assign)*)? ")"
static Node *funcall(Token **rest, Token *tok) {
  Node *node = new_node(ND_FUNCALL, tok);
  node->funcname = get_ident(tok);

  Token *start = tok; 
  tok = tok->next->next;

  VarScope *sc = find_var(start);
  if (!sc)
    error_tok(start, "暗黙の関数宣言です");
  if (!sc->var || sc->var->ty->kind != TY_FUNC)
    error_tok(start, "関数ではありません");

  Type *ty = sc->var->ty;
  Type *param_ty = ty->params;

  Node head = {};
  Node *cur = &head;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok = skip(tok, ",");

    Node *arg = assign(&tok, tok);
    add_type(arg);

    if (param_ty) {
      if (param_ty->kind == TY_STRUCT || param_ty->kind == TY_UNION)
        error_tok(arg->tok, "構造体や共用体はまだサポートしていません");

      arg = new_cast(arg, param_ty);
      param_ty = param_ty->next;
    }

    cur = cur->next = arg;
  }

  *rest = skip(tok, ")");
  node->ty = ty->return_ty;
  node->func_ty = ty;
  node->args = head.next;
  return node;
}

// A++を (Aの型)((A += 1) - 1)に変換する
static Node *new_inc_dec(Node *node, Token *tok, int addend) {
  add_type(node);
  
  // A += 1
  Node *assign = to_assign(new_add(node, new_num(addend, tok), tok));

  // ((A += 1) - 1)
  Node *n = new_add(assign, new_num(-addend, tok), tok);

  return new_cast(n, node->ty);
}

// postfix = primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")*
static Node *postfix(Token **rest, Token *tok) {
  Node *node = primary(&tok, tok);

  for (;;) {
    if (equal(tok, "[")) {
      // x[y]は*(x+y)として解釈される
      Token *start = tok;
      Node *index = expr(&tok, tok->next);
      tok = skip(tok, "]");
      node = new_unary(ND_DEREF, new_add(node, index, start), start);
      continue;
    }

    if (equal(tok, ".")) {
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }

    if (equal(tok, "->")) {
      node = new_unary(ND_DEREF, node, tok);
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }

    if (equal(tok, "++")) {
      node = new_inc_dec(node, tok, 1);
      tok = tok->next;
      continue;
    }

    if (equal(tok, "--")) {
      node = new_inc_dec(node, tok, -1);
      tok = tok->next;
      continue;
    }

    *rest = tok;
    return node;
  }
}

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")" | funcall | ident | num | str
static Node *primary(Token **rest, Token *tok) {
  // GNU statement expression
  if (equal(tok, "(") && equal(tok->next, "{")) {
    Node *node = new_node(ND_STMT_EXPR, tok);
    node->body = compound_stmt(&tok, tok->next->next)->body;
    *rest = skip(tok, ")");
    return node;
  }

  if (equal(tok, "(")) {
    Node *node = expr(&tok, tok->next);
    *rest = skip(tok, ")");
    return node;
  }

  if (tok->kind == TK_IDENT) {
    // 関数呼び出し
    if (equal(tok->next, "("))
      return funcall(rest, tok);

    // 変数
    VarScope *sc = find_var(tok);
    if (!sc || (!sc->var && !sc->enum_ty))
      error_tok(tok, "未定義の変数です");

    Node *node;
    if (sc->var)
      node = new_var_node(sc->var, tok);
    else
      node = new_num(sc->enum_val, tok);

    *rest = tok->next;
    return node;
  }

  if (tok->kind == TK_NUM) {
    Node *node = new_num(tok->val, tok);
    *rest = tok->next;
    return node;
  }

  if (tok->kind == TK_STR) {
    Obj *var = new_string_literal(tok->str, tok->ty);
    *rest = tok->next;
    return new_var_node(var, tok);
  }

  error_tok(tok, "式でないといけません");
}

static Token *parse_typedef(Token *tok, Type *basety) {
  bool first = true;

  while (!consume(&tok, tok, ";")) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    Type *ty = declarator(&tok, tok, basety);
    push_scope(get_ident(ty->name))->type_def = ty;
  }

  return tok;
}

// スタックに積む順番を揃えるため逆転
static void create_param_lvars(Type *param) {
  if (param) {
    create_param_lvars(param->next);
    new_lvar(get_ident(param->name), param);
  }
}

// function = declspec declarator (";" | "{" compound-stmt)
static Token *function(Token *tok, Type *basety, VarAttr *attr) {
  Type *ty = declarator(&tok, tok, basety);

  locals = NULL;

  Obj *fn = new_gvar(get_ident(ty->name), ty);
  fn->is_function = true;
  fn->is_definition = !consume(&tok, tok, ";");
  fn->is_static = attr->is_static;

  if (!fn->is_definition)
    return tok;

  current_fn = fn;
  enter_scope();
  create_param_lvars(ty->params);
  fn->params = locals;

  tok = skip(tok, "{");
  fn->body = compound_stmt(&tok, tok);
  fn->locals = locals;
  leave_scope();
  resolve_goto_labels();
  return tok;
}

// global-variable = declspec (declarator ("," declarator)*)? ";"
static Token *global_variable(Token *tok, Type *basety) {
  bool first = true;
  while (!equal(tok, ";")) {
    if (!first)
      tok = skip(tok, ",");

    first = false;
    Type *ty = declarator(&tok, tok, basety);
    new_gvar(get_ident(ty->name), ty);
  }

  return skip(tok, ";");
}

static bool is_function(Token *tok) {
  if (equal(tok, ";"))
    return false;

  Type dummy = {};
  Type *ty = declarator(&tok, tok, &dummy);
  return ty->kind == TY_FUNC;
}

// program = (typedef | function | global-variable)*
Obj *parse(Token *tok) {
  globals = NULL;

  while (tok->kind != TK_EOF) {
    VarAttr attr = {};
    Type *basety = declspec(&tok, tok, &attr);

    if (attr.is_typedef) {
      tok = parse_typedef(tok, basety);
      continue;
    }

    if (is_function(tok))
      tok = function(tok, basety, &attr);
    else
      tok = global_variable(tok, basety);
  }

  return globals;
}

