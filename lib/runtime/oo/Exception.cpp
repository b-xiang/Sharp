//
// Created by BraxtonN on 2/12/2018.
//

#include "Exception.h"
#include "../Environment.h"
#include "ClassObject.h"
#include "../Thread.h"
#include "../register.h"
#include "../memory/GarbageCollector.h"

void Throwable::drop() {
    this->throwable = NULL;
    this->message.free();
    stackTrace.free();
}

Exception::Exception(const char *msg, bool native)
        :
        throwable(Environment::RuntimeErr, msg, native),
        runtime_error(msg)
{
    pushException();
}

Exception::Exception(const std::string &__arg, bool native)
        :
        throwable(Environment::RuntimeErr, __arg, native),
        runtime_error(__arg)
{
    pushException();
}

Exception::Exception(ClassObject* throwable, const std::string &__arg, bool native)
        :
        throwable(throwable, __arg, native),
        runtime_error(__arg)
{
    pushException();
}

Exception::~Exception()
{
    throwable.drop();
}

void Exception::pushException() {
    if(thread_self != NULL && throwable.native) {
        if(throwable.message == "out of memory") {
            /*
             * If there is no memory we exit
             */
            thread_self->state = THREAD_KILLED;
            return;
        }

        (++thread_self->sp)->object
                = GarbageCollector::self->newObject(throwable.throwable);
    }
}

Exception::Exception(Throwable &throwable)
        :
        runtime_error("")
{
    this->throwable.init();
    this->throwable = throwable;
}