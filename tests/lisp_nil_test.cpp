// SPDX-License-Identifier: GPL-2.0-or-later
// The legacy interpreter represents nil with a null LObject pointer.
#include <cstdio>
#include <cstdlib>
#include "common.h"
#include "lisp.h"
#include "status.h"
#include "dprint.h"
#include "lisp_gc.h"
static void require(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "Lisp regression: %s\n", message); exit(1); }
}
int main()
{
    set_dprinter([](char *text) { fputs(text, stderr); });
    set_dgetter([](char *, int) { fputs("Unexpected Lisp debugger entry\n", stderr); exit(1); });
    stat_man = new text_status_manager();
    lisp_init();
    fputs("Checking nil conditions\n", stderr);
    const char *program = "(if nil 1 2)";
    LObject *form = LObject::Compile(program);
    require(lnumber_value(form->Eval()) == 2, "nil condition chooses false branch in optimized builds");
    fputs("Checking nil variable\n", stderr);
    program = "(progn (setq test-option nil) (if test-option 3 4))";
    form = LObject::Compile(program);
    require(lnumber_value(form->Eval()) == 4, "nil variable evaluates correctly");
    for (int i = 0; i < 2000; ++i) {
        const char *code = "(list 1 2 3)";
        LObject *expression = LObject::Compile(code);
        PtrRef expression_root(expression);
        LObject *result = expression->Eval();
        require(result && lnumber_value(CAR(result)) == 1, "evaluation roots survive repeated garbage collection");
    }
    const auto roots_before = PtrRef::stack.m_size;
    program = "(mapcar 'car nil)";
    form = LObject::Compile(program);
    require(form->Eval() == nullptr, "mapcar over nil returns nil");
    require(PtrRef::stack.m_size == roots_before, "mapcar removes temporary roots on an empty list");
    {
        LObject *list = nullptr;
        PtrRef list_root(list);
        for (int i = 0; i < 4000; ++i) {
            LList *cell = LList::Create();
            PtrRef cell_root(cell);
            LObject *number = LNumber::Create(i);
            cell->car = number;
            cell->cdr = list;
            list = cell;
        }
        fputs("Checking collected list\n", stderr);
        LispGC::CollectSpace(TMP_SPACE, 0);
        for (int i = 3999; i >= 0; --i) {
            if (!list || !CAR(list)) { fprintf(stderr, "Missing cell/car at %d\n", i); exit(1); }
            require(list && lnumber_value(CAR(list)) == i, "collector preserves rooted lists across allocations");
            list = CDR(list);
        }
        require(list == nullptr, "collected list terminates with nil");
    }
    lisp_uninit();
    delete stat_man; stat_man = nullptr;
    puts("Lisp nil regression checks passed.");
}
