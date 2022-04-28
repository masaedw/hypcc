#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "9cc.h"

// エラーを報告するための関数
// printfと同じ引数を取る
void error(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

int new_label()
{
    static int count = 0;
    return count++;
}

// FPにoffsetを足したものをスタックにプッシュする
void gen_lval(Node *node)
{
    if (node->kind != ND_LVAR)
        error("代入の左辺値が変数ではありません");

    printf("  add X0, FP, #%d\n", node->offset);
    printf("  str X0, [SP, #-16]!\n");
}

void gen(Node *node)
{
    switch (node->kind)
    {
    case ND_NUM:
        printf("  mov X0, #%d\n", node->val);
        printf("  str X0, [SP, #-16]!\n");
        return;
    case ND_LVAR:
        if (node->kind != ND_LVAR)
            error("代入の左辺値が変数ではありません");
        printf("  ldur X0, [FP, #%d]\n", node->offset);
        printf("  str X0, [SP, #-16]!\n");
        return;
    case ND_ASSIGN:
        if (node->lhs->kind != ND_LVAR)
            error("代入の左辺値が変数ではありません");
        gen(node->rhs);

        printf("  ldur X0, [SP, #0]\n");
        printf("  stur X0, [FP, #%d]\n", node->lhs->offset);
        return;
    case ND_RETURN:
        gen(node->lhs);
        int offset = locals->offset + 8;
        if (offset % 16 != 0)
        {
            offset += 8;
        }
        printf("  ldr X0, [SP], #16\n");
        // エピローグ
        printf("  mov SP, FP\n");
        printf("  add SP, SP, #%d\n", offset);
        printf("  ldp LR, FP, [SP], #16\n");
        printf("  ret\n");
        return;
    case ND_IF:
    {
        // 最後に評価した値をスタックトップに残す
        int label = new_label();
        printf("  ; start if expr %03d\n", label);
        gen(node->expr0);
        printf("  ; end if expr %03d\n", label);
        printf("  ldur X0, [SP, #0]\n");
        if (!node->rhs)
        {
            printf("  cbz X0, LEND_%03d\n", label);
            printf("  ldr X0, [SP], #16\n");
            printf("  ; start then %03d\n", label);
            gen(node->lhs);
            printf("LEND_%03d:\n", label);
            printf("  ; end then %03d\n", label);
        }
        else
        {
            printf("  cbz X0, LELSE_%03d\n", label);
            printf("  ldr X0, [SP], #16\n");
            printf("  ; start then %03d\n", label);
            gen(node->lhs);
            printf("  ; end then %03d\n", label);
            printf("  b LEND_%03d\n", label);
            printf("LELSE_%03d:\n", label);
            printf("  ldr X0, [SP], #16\n");
            printf("  ; start else %03d\n", label);
            gen(node->rhs);
            printf("  ; end else %03d\n", label);
            printf("LEND_%03d:\n", label);
        }
        return;
    }
    case ND_WHILE:
    {
        // 条件式の結果をスタックトップに残す
        int label = new_label();
        printf("LBEGIN_%03d:\n", label);
        printf("  ; start while expr %03d\n", label);
        gen(node->expr0);
        printf("  ; end while expr %03d\n", label);
        printf("  ldur X0, [SP, #0]\n");
        printf("  cbz X0, LEND_%03d\n", label);
        printf("  ldr X0, [SP], #16\n");
        printf("  ; start while body %03d\n", label);
        gen(node->lhs);
        printf("  ; end while body %03d\n", label);
        printf("  ldr X0, [SP], #16\n");
        printf("  b LBEGIN_%03d\n", label);
        printf("LEND_%03d:\n", label);
        return;
    }
    case ND_FOR:
    {
        // for (expr0; expr1; rhs) lhs

        // 条件式 expr1がある場合、条件式の結果をスタックトップに残す
        // ない場合、returnで抜けるはずなので考えない
        int label = new_label();
        if (node->expr0)
        {
            printf("  ; start for expr0 %03d\n", label);
            gen(node->expr0);
            printf("  ; end for expr0 %03d\n", label);
            printf("  ldr X0, [SP], #16\n");
        }
        printf("LBEGIN_%03d:\n", label);
        if (node->expr1)
        {
            printf("  ; start for expr1 %03d\n", label);
            gen(node->expr1);
            printf("  ; end for expr1 %03d\n", label);
            printf("  ldur X0, [SP, #0]\n");
            printf("  cbz X0, LEND_%03d\n", label);
        }
        printf("  ldr X0, [SP], #16\n");
        printf("  ; start for lhs %03d\n", label);
        gen(node->lhs);
        printf("  ; end for lhs %03d\n", label);
        printf("  ldr X0, [SP], #16\n");
        if (node->rhs)
        {
            printf("  ; start for rhs %03d\n", label);
            gen(node->rhs);
            printf("  ; end for rhs %03d\n", label);
            printf("  ldr X0, [SP], #16\n");
        }
        printf("  b LBEGIN_%03d\n", label);
        printf("LEND_%03d:\n", label);
        return;
    }
    case ND_BLOCK:
        // 最後に評価した値をスタックトップに残す。
        // bodyが空の場合は0をプッシュしておく。
        printf("  str XZR, [SP, #-16]!\n");
        for (int i = 0; node->body[i]; i++)
        {
            printf("  ldr X0, [SP], #16\n");
            gen(node->body[i]);
        }
        return;
    case ND_CALL:
        for (int i = 0; i < node->nargs; i++)
        {
            gen(node->args[i]);
        }
        for (int i = 0; i < node->nargs; i++)
        {
            printf("  ldur X%d, [SP, #%d]\n", i, 16 * (node->nargs - i - 1));
        }
        printf("  add sp, sp, #%d\n", 16 * node->nargs);
        printf("  bl _%.*s\n", node->len, node->name);
        printf("  str X0, [SP, #-16]!\n");
        return;
    case ND_FUNDEF:
    {
        printf("  .global  _main  ; -- start function %.*s\n", node->len, node->name);
        printf("  .p2align  2\n");
        printf("_%.*s:\n", node->len, node->name);

        // プロローグ
        // 変数分の領域を16バイト境界で確保する
        int offset = locals->offset + 8;
        if (offset % 16 != 0)
        {
            offset += 8;
        }
        printf("  stp LR, FP, [SP, #-16]!\n");
        printf("  sub FP, SP, #%d\n", offset);
        printf("  sub SP, SP, #%d\n", offset);
        for (int i = 0; i < node->nargs; i++)
        {
            printf("  stur X%d, [FP, #%d]\n", i, node->args[i]->offset);
        }

        // 先頭の式から順にコード生成
        for (int i = 0; node->body[i]; i++)
        {
            gen(node->body[i]);

            // 式の評価結果としてスタックに一つの値が残っている
            // はずなので、スタックが溢れないようにポップしておく
            printf("  ldr X0, [SP], #16\n");
        }

        // エピローグ
        // 最後の式の結果がX0に残っているのでそれが返り値になる
        printf("  mov SP, FP\n");
        printf("  add SP, SP, #%d\n", offset);
        printf("  ldp LR, FP, [SP], #16\n");
        printf("  ret\n");
        printf("; -- end function %.*s\n", node->len, node->name);
        return;
    }
    case ND_ADDR:
        gen_lval(node->lhs);
        return;
    case ND_DEREF:
        gen(node->lhs);
        printf("  ldr X0, [SP], #16\n");
        printf("  ldur X0, [X0, #0]\n");
        printf("  str X0, [SP, #-16]!\n");
        return;
    case ND_VARDEF:
        // dummy push
        printf("  str XZR, [SP, #-16]!\n");
        return;
    }

    gen(node->lhs);
    gen(node->rhs);

    printf("  ldr X0, [SP], #16\n"); // rhs
    printf("  ldr X1, [SP], #16\n"); // lhs

    // X1 op X0

    switch (node->kind)
    {
    case ND_ADD:
        printf("  add X0, X1, X0\n");
        break;
    case ND_SUB:
        printf("  sub X0, X1, X0\n");
        break;
    case ND_MUL:
        printf("  mul X0, X1, X0\n");
        break;
    case ND_DIV:
        printf("  sdiv X0, X1, X0\n");
        break;
    case ND_LT:
        printf("  cmp X1, X0\n");
        printf("  cset X0, LO\n");
        break;
    case ND_LE:
        printf("  cmp X1, X0\n");
        printf("  cset X0, LS\n");
        break;
    case ND_EQ:
        printf("  cmp X1, X0\n");
        printf("  cset X0, EQ\n");
        break;
    case ND_NE:
        printf("  cmp X1, X0\n");
        printf("  cset X0, NE\n");
        break;
    }

    printf("  str X0, [SP, #-16]!\n");
}
