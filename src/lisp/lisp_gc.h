// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#include "stack.h"
#ifndef __LISP_GC_HPP_
#define __LISP_GC_HPP_

// Stack user progs can push data and have it GCed
extern GrowStack<void> l_user_stack;

class LispGC
{
public:
    // Collect temporary or permanent spaces
    static void CollectSpace(int which_space, int grow);

private:
    static LArray *CollectArray(LArray *x);
    static LList *CollectList(LList *x);
    static LObject *CollectObject(LObject *x);
    static void CollectSymbols(LSymbol *root);
    static void CollectStacks();
};

// This pointer reference stack lists all pointers to temporary lisp
// objects. This allows the pointers to be automatically modified if an
// object allocation triggers a garbage collection.
class PtrRef
{
public:
    template<typename T> inline PtrRef(T *&ref)
    {
        stack.push((void **)&ref);
    }

    // A temporary pointer (notably `this`) dies at the end of the statement.
    // The collector must only register a mutable pointer with a real scope.
    template<typename T> PtrRef(T * const &ref) = delete;

    inline ~PtrRef()
    {
        stack.pop(1);
    }

    // Stack of user pointers, user pointers get remapped on GC
    static GrowStack<void *> stack;
};

#endif

