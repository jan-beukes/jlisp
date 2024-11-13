#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mpc.h"

#define true 1
#define false 0

#define JLISP_VERSION "0.1"
#define PROMPT ">>"

// wraping fgets as readline substitute for windows
#ifdef _WIN32
static char buffer[2048];

char *readline(const char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char *cpy = malloc(strlen(buffer) + 1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy) - 1] = '\0'; // remove newline
  return cpy;
}

void add_history(char* unused) {}
#else 
#include <editline/readline.h>
#endif //_WIN32

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) { \
    lval *err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args); \
    return err; \
    } 


// forward declaration

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

// lval type
enum {
    LVAL_NUM,
    LVAL_ERR,
    LVAL_SYM,
    LVAL_FUN,
    LVAL_SEXPR,
    LVAL_QEXPR,
};

typedef lval *(*lbuiltin)(lenv *, lval *);

struct lval {
    int type;

    long num;
    char *err;
    char *sym;
    lbuiltin fun;

    // count and array of *lval
    int count;
    struct lval **cell;
};

struct lenv {
    int count;
    char **syms;
    lval **vals;
};

// function definitions
void lval_print(lval *v);
lval *lval_eval(lenv *e, lval *v);
lenv *lenv_new();
void lenv_del(lenv *e);
lval *lenv_get(lenv *e, lval *k);
void lenv_put(lenv *e, lval *k, lval *v);

long pow_long(long x, long n) {
    if (n <= 0) return 1;

    long res = 1;
    for (int i = 0; i < n; i++) res *= x; 
    return res;
}


// create lval of type num
lval *lval_num(long x) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

// create lval of type err
lval *lval_err(char *fmt, ...) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_ERR;

    // create and initialize va list
    va_list va;
    va_start(va, fmt);

    v->err = malloc(512);

    vsnprintf(v->err, 511, fmt, va);

    v->err = realloc(v->err, strlen(v->err) + 1);

    va_end(va);
    return v;
}

// create lval of type symbol
lval *lval_sym(char *s) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(sizeof(strlen(s)) + 1);
    strcpy(v->sym, s);
    return v;
}

lval *lval_fun(lbuiltin func) {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->fun = func;
    return v;
}

// create lval of type Sexpr (list of expressions)
lval *lval_sexpr() {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

// create lval of type Qexpr 
lval *lval_qexpr() {
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

char *ltype_name(int t) {
    switch (t) {
        case LVAL_FUN: return "Function";
        case LVAL_NUM: return "Number";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        default: return "Unknown";
    }
}

void lval_del(lval *v) {

    switch (v->type) {

        case LVAL_NUM: 
            break;
        case LVAL_ERR: free(v->err);
            break;
        case LVAL_SYM: free(v->sym);
            break;
        case LVAL_QEXPR:
        case LVAL_SEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            free(v->cell);
            break;
        case LVAL_FUN: break;
    }
    free(v);
}

lval *lval_add(lval *v, lval *x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count - 1] = x;
    return v;
}

// Deep Copy
lval *lval_copy(lval *v) {

    lval *x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {
        case LVAL_FUN: x->fun = v->fun;
            break;
        case LVAL_NUM: x->num = v->num;
            break;

        case LVAL_ERR: 
            x->err = malloc(strlen(v->err) + 1);
            strcpy(x->err, v->err);
            break;

        case LVAL_SYM:
            x->sym = malloc(strlen(v->sym) + 1);
            strcpy(x->sym, v->sym);
            break;

        case LVAL_SEXPR:
        case LVAL_QEXPR:
            x->count = v->count;
            x->cell = malloc(sizeof(lval *) * x->count);
            for (int i = 0; i < x->count; i ++) {
                x->cell[i]= lval_copy(v->cell[i]);
            }
            break;
    }
    return x;
}

lval *lval_read_num(mpc_ast_t *t) {
    errno = 0;
    long x = strtol(t->contents, NULL, 10); //strtol sets errno for errors
    return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

lval *lval_read(mpc_ast_t *t) {
    // Symbol or Number
    if (strstr(t->tag, "number")) return lval_read_num(t);
    if (strstr(t->tag, "symbol")) return lval_sym(t->contents);

    // If root (>) or sexpr create empty list
    lval *x = NULL;
    if (strcmp(t->tag, ">") == 0) x = lval_sexpr();
    if (strstr(t->tag, "sexpr")) x = lval_sexpr();
    if (strstr(t->tag, "qexpr")) x = lval_qexpr();


    // fill list with valid expressions
    for (int i = 0; i < t->children_num; i++) {
        if (strcmp(t->children[i]->contents, "(") == 0) continue;
        if (strcmp(t->children[i]->contents, ")") == 0) continue;
        if (strcmp(t->children[i]->contents, "{") == 0) continue;
        if (strcmp(t->children[i]->contents, "}") == 0) continue;
        if (strcmp(t->children[i]->tag, "regex") == 0) continue;
        
        x = lval_add(x, lval_read(t->children[i]));
    }

    return x;
}

void lval_expr_print(lval *v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);
        
        if (i != (v->count - 1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

void lval_print(lval *v) {
    switch (v->type) {
        case LVAL_NUM: printf("%li", v->num);
            break;
        case LVAL_ERR: printf("Error: %s", v->err);
            break;
        case LVAL_SYM: printf("%s", v->sym);
            break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')');
            break;
        case LVAL_QEXPR: lval_expr_print(v, '{', '}');
            break;
        case LVAL_FUN: printf("<function>");
            break;
    }
}

void lval_println(lval *v) {
    lval_print(v);
    putc('\n', stdout);
}

lval *lval_pop(lval *v, int i) {

    lval *x = v->cell[i];
    
    // shift memory after i back one
    memmove(&v->cell[i], &v->cell[i+1],
            sizeof(lval*) * (v->count - i-1));

    v->count--;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    return x;
}

lval *lval_take(lval *v, int i) {
    lval *x = lval_pop(v, i);
    lval_del(v);
    return x;
}

lval *lval_join(lval *x, lval *y) {

    // for each cell in x add it to y
    while (y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    lval_del(y);
    return x;
}


lval *builtin_op(lenv *e, lval *a, char *op) {
    
    // Ensure all numbers
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number");
        }
    }

    //first element
    lval *x = lval_pop(a, 0);

    // unary negation
    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    while (a->count > 0) {

        lval *y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) x->num += y->num;
        if (strcmp(op, "-") == 0) x->num -= y->num;
        if (strcmp(op, "*") == 0) x->num *= y->num;
        if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division by zero!");
                break;
            }
        }
        if (strcmp(op, "^") == 0) {
            x->num = pow_long(x->num, y->num);
        }

        lval_del(y);
    }

    lval_del(a);
    return x;
}

lval *builtin_add(lenv *e, lval *a) {
  return builtin_op(e, a, "+");
}

lval *builtin_sub(lenv *e, lval *a) {
  return builtin_op(e, a, "-");
}

lval *builtin_mul(lenv *e, lval *a) {
  return builtin_op(e, a, "*");
}

lval *builtin_div(lenv *e, lval *a) {
  return builtin_op(e, a, "/");
}

lval *builtin_pow(lenv *e, lval *a) {
    return builtin_op(e, a, "^");
}

lval *builtin_head(lenv *e, lval *a) {
    // Error conditions
    LASSERT(a, a->count == 1,
        "Function 'head' passed too many arguments. "
        "Got %i, Expected %i.",
        a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
        "Function 'head' passed incorrect type!");
    LASSERT(a, a->cell[0]->count != 0,
        "Function 'head' passed {}!");

    // Take first element
    lval *v = lval_take(a, 0);
    // Delete all elements that are not head
    while (v->count > 1) lval_del(lval_pop(v, 1));
    return v;
}

lval *builtin_tail(lenv *e, lval *a) {
    // Error conditions
    LASSERT(a, a->count == 1,
        "Function 'tail' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
        "Function 'tail' passed incorrect type!");
    LASSERT(a, a->cell[0]->count != 0,
        "Function 'tail' passed {}!");

    // Take first element
    lval *v = lval_take(a, 0);

    // Delete it's first element
    lval_del(lval_pop(v, 0));
    return v;

}

lval *builtin_list(lenv *e, lval *a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval *builtin_eval(lenv *e, lval *a) {
    LASSERT(a, a->count == 1,
            "Function 'eval' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'eval' passed incorrect type!");

    lval *x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

lval *builtin_join(lenv *e, lval *a) {
    // Make sure all are Qexpr
    for (int i = 0; i < a->count; i++) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect type!");
    }

    lval *x = lval_pop(a, 0);

    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }
    lval_del(a);
    return x;
}

lval *builtin_def(lenv *e, lval *a) {
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
        "Function 'def' passed incorrect type!");

    // first argument is symbol list
    lval *syms = a->cell[0];

    // ensure all elements of first list are symbols
    for (int i = 0; i < syms->count; i++) {
        LASSERT(a, syms->cell[i]->type == LVAL_SYM,
                "Function 'def' cannot define non-symbol");
    }

    // check correct number of values to symbols
    LASSERT(a, syms->count == a->count - 1,
            "Function 'def' cannot define incorrect "
            "number of value to symbols");

    // assign copies of value to symbols
    for (int i = 0; i < syms->count; i++) {
        lenv_put(e, syms->cell[i], a->cell[i + 1]);
    }

    lval_del(a);
    return lval_sexpr();
}

lval *lval_eval_sexpr(lenv *e, lval *v) {

    // Evaluate children
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(e, v->cell[i]);
    }
    
    // Check children for errors
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) return lval_take(v, i);
    }

    // Empty expression
    if (v->count == 0) return v;
    // Single expression
    if (v->count == 1) return lval_take(v, 0);

    // Ensure first element is a function after eval
    lval *f = lval_pop(v, 0);
    if (f->type != LVAL_FUN) {
        lval_del(v);
        lval_del(f);
        return lval_err("first element is not a function!");
    }

    // call function to get result
    lval *result = f->fun(e, v);
    lval_del(f);
    return result;
}

lval *lval_eval(lenv *e, lval *v) {
    if (v->type == LVAL_SYM) {
        lval *x = lenv_get(e, v);
        lval_del(v);
        return x;
    }
    if (v->type == LVAL_SEXPR) return lval_eval_sexpr(e, v);

    return v;
}

lenv *lenv_new() {
    lenv *e = malloc(sizeof(lval));
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

void lenv_del(lenv *e) {
    for (int i = 0; i < e->count; i++) {
        free(e->syms[i]); // syms are strings
        lval_del(e->vals[i]);

    }
}

// get lval from enviroment with given key (sym -> fun)
lval *lenv_get(lenv *e, lval *k) {
    
    for (int i = 0; i < e->count; i++) {
        if (strcmp(e->syms[i], k->sym) == 0) {
            return lval_copy(e->vals[i]);
        }
    }

    return lval_err("Unbound symbol '%s'", k->sym);
}

void lenv_put(lenv *e, lval *k, lval *v) {
    // check if already exists
    // and replace with v
    for (int i = 0; i < e->count; i++) {
        if(strcmp(e->syms[i], k->sym) == 0) {
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    // allocate for new entry
    e->count++;
    e->vals = realloc(e->vals, sizeof(lval *) * e->count);
    e->syms = realloc(e->syms, sizeof(char *) * e->count);


    e->vals[e->count - 1] = lval_copy(v);
    e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
    strcpy(e->syms[e->count - 1], k->sym);
}

void lenv_add_builtin(lenv *e, char *name, lbuiltin func) {
    lval *k = lval_sym(name);
    lval *v = lval_fun(func);
    lenv_put(e, k, v);

    lval_del(k);
    lval_del(v);
}

void lenv_add_builtins(lenv *e) {
    // List Functions
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);

    // Math functions
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);
    lenv_add_builtin(e, "^", builtin_pow);

    // Variable Functions
    lenv_add_builtin(e, "def", builtin_def);
}

int main(int argc, char *argv[]) {
    // create parsers
    mpc_parser_t *number = mpc_new("number");
    mpc_parser_t *symbol = mpc_new("symbol");
    mpc_parser_t *sexpr = mpc_new("sexpr");
    mpc_parser_t *qexpr = mpc_new("qexpr");
    mpc_parser_t *expr = mpc_new("expr");
    mpc_parser_t *lispy = mpc_new("lispy");

    // parser language definitions
    mpca_lang(MPCA_LANG_DEFAULT,

        "number   : /-?[0-9]+/ ;                               "
        "symbol   : /[a-zA-Z0-9_+\\-*^\\/\\\\=<>!&]+/ ;         "
        "sexpr    : '(' <expr>* ')' ;                          "
        "qexpr    : '{' <expr>* '}' ;                          "
        "expr     : <number> | <symbol> | <sexpr> | <qexpr> ;  "
        "lispy    : /^/ <expr>* /$/ ;                          ",

              number, symbol, sexpr, qexpr, expr, lispy);

    printf("Jlisp Version %s\n", JLISP_VERSION);
    printf("Press Ctrl+c to Exit\n");

    // Environment
    lenv *e = lenv_new();
    lenv_add_builtins(e);

    while (true) {
        char *input = readline(PROMPT);
        add_history(input);

        mpc_result_t r;
        if (mpc_parse("<stdin>", input, lispy, &r)) {
            lval *x = lval_eval(e, lval_read(r.output));
            lval_println(x);
            lval_del(x);

            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        free(input);
    }
    lenv_del(e);
    mpc_cleanup(6, number, symbol, sexpr, qexpr, expr, lispy);
    return 0;

}