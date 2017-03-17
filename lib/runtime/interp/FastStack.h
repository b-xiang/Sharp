//
// Created by BraxtonN on 2/24/2017.
//

#ifndef SHARP_FASTSTACK_H
#define SHARP_FASTSTACK_H

#include "../../../stdimports.h"
#include "../oo/string.h"

#define default_stack 0xffe

class gc_object;

struct s_it{
    double n;
    void* v;
};

class FastStack {
public:
    FastStack()
    :
            stack(NULL),
            len(0),
            sp(-1)
    {
    }

    void init() {
        stack = new s_it[default_stack];
        len = default_stack;
    }

    int32_t len;

    CXX11_INLINE
    void push(gc_object* value){
        stack[++sp].v = value;
    }
    CXX11_INLINE
    void push(double value){
        stack[++sp].n = value;
    }

    CXX11_INLINE
    gc_object* pop(){
        return (gc_object*)stack[sp--].v;
    }

    CXX11_INLINE
    double popn(){
        return stack[sp--].n;
    }

    void free();

    void popvoid();

    void swap();

private:
    s_it* stack;
    int32_t sp;

};


#endif //SHARP_FASTSTACK_H