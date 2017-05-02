//
// Created by BraxtonN on 2/24/2017.
//

#include "Thread.h"
#include "Exe.h"
#include "../interp/vm.h"
#include "../interp/Opcode.h"
#include "Environment.h"
#include "../alloc/GC.h"
#include "../interp/register.h"

int32_t Thread::tid = 0;
thread_local Thread* thread_self = NULL;
Thread** Thread::threads = NULL;
unsigned int Thread::tp = 0;

/*
 * Local registers for the thread to use
 */
thread_local double __rxs[10];

void Thread::Startup() {
    threads = (Thread**)malloc(sizeof(Thread**)*MAX_THREADS);
    for(unsigned int i = 0; i < MAX_THREADS; i++) {
        threads[i] = NULL;
    }

    Thread* main = (Thread*)malloc(
            sizeof(Thread)*1);
    main->main = &env->__address_spaces[manifest.entry];
    main->Create("Main");
}

void Thread::Create(string name, ClassObject* klass, int64_t method) {
    this->monitor = Monitor();
    this->name.init();

    this->main = NULL;//env->getMethodFromClass(klass, method);
    this->name = name;
    this->id = Thread::tid++;
    this->__stack = (data_stack*)memalloc(sizeof(data_stack)*STACK_SIZE);
    this->suspendPending = false;
    this->exceptionThrown = false;
    this->suspended = false;
    this->exited = false;
    this->daemon = false;
    this->state = thread_init;
    this->exitVal = 0;
    this->sp = -1;
    this->fp = 0;

    push_thread(this);
}

void Thread::Create(string name) {
    this->monitor = Monitor();
    this->name.init();

    this->name = name;
    this->id = Thread::tid++;
    this->__stack = (data_stack*)memalloc(sizeof(data_stack)*STACK_SIZE);
    this->suspendPending = false;
    this->exceptionThrown = false;
    this->suspended = false;
    this->exited = false;
    this->daemon = false;
    this->state = thread_init;
    this->exitVal = 0;
    this->sp = -1;
    this->fp = 0;

    push_thread(this);
}

void Thread::CreateDaemon(string) {
    this->monitor = Monitor();
    this->name.init();

    this->name = name;
    this->id = Thread::tid++;
    this->__stack = NULL;
    this->suspendPending = false;
    this->exceptionThrown = false;
    this->suspended = false;
    this->exited = false;
    this->daemon = true;
    this->state = thread_init;
    this->exitVal = 0;
    this->sp = -1;
    this->fp = 0;

    push_thread(this);
}

void Thread::push_thread(Thread *thread) const {
    bool threadSet = false;
    for(unsigned int i = 0; i < tp; i++) {
        if(threads[i] == NULL) {
            threads[i] = thread;
            threadSet = true;
        }
    }

    if(!threadSet)
        threads[tp++]=thread;
}

Thread *Thread::getThread(int32_t id) {
    for(int32_t i = 0 ; i < tp; i++) {
        Thread* thread = threads[i];

        if(thread != NULL && thread->id == id)
            return thread;
    }

    return NULL;
}

void Thread::suspendSelf() {
    thread_self->suspended = true;
    thread_self->suspendPending = false;

    /*
	 * We call wait upon suspend. This function will
	 * sleep the thread most of the time. notify() or
	 * resumeAllThreads() should be called to revive thread.
	 */
    thread_self->wait();
}

void Thread::wait() {
    const long sMaxRetries = 128 * 1024;

    long spinCount = 0;
    long retryCount = 0;

    this->state = thread_suspend;

    while (this->suspended)
    {
        if (retryCount++ == sMaxRetries)
        {
            spinCount++;
            retryCount = 0;
#ifdef WIN32_
            Sleep(5);
#endif
#ifdef POSIX_
            usleep(5*POSIX_USEC_INTERVAL);
#endif
        } else if(this->state == thread_killed) {
            this->suspended = false;
            return;
        }
    }

    this->state = thread_running;
}

int Thread::start(int32_t id) {
    Thread *thread = getThread(id);

    if (thread == NULL)
        return 1;

    if(thread->state == thread_running)
        return 2;

    thread->exited = false;
    thread->state = thread_init;
#ifdef WIN32_
    thread->thread = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size
            &SharpVM::InterpreterThreadStart,       // thread function caller
            thread,                 // thread self when thread is created
            0,                      // use default creation flags
            NULL);
    if(thread->thread == NULL) return 3; // thread was not started
    else
        return waitForThread(thread);
#endif
#ifdef POSIX_
    if(pthread_create( &thread->thread, NULL, vm->InterpreterThreadStart, (void*) thread))
        return 3; // thread was not started
    else {
        return waitForThread(thread);
    }
#endif

}

int Thread::interrupt(int32_t id) {
    if(id == thread_self->id)
        return 1; // cannot interrupt thread_self

    Thread* thread = getThread(id);
    if(thread == NULL || thread->daemon)
        return 1;

    return interrupt(thread);
}

void Thread::waitForThreadSuspend(Thread *thread) {
    const int sMaxRetries = 10000000;
    const int sMaxSpinCount = 25;

    int spinCount = 0;
    int retryCount = 0;

    suspendThread(thread);
    while (thread->state == thread_running && !thread->suspended)
    {
        if (retryCount++ == sMaxRetries)
        {
            retryCount = 0;
            if(++spinCount >= sMaxSpinCount)
            {
                return; // give up
            } else if(thread->state >= thread_suspend)
                return;
        }
    }
}

void Thread::waitForThreadExit(Thread *thread) {
    const int sMaxRetries = 10000000;
    const int sMaxSpinCount = 25;

    int spinCount = 0;
    int retryCount = 0;

    while (!thread->exited)
    {
        if (retryCount++ == sMaxRetries)
        {
            retryCount = 0;
            if(++spinCount >= sMaxSpinCount)
            {
                return; // give up
            } else if(thread->exited)
                return;
        }
    }
}

int Thread::waitForThread(Thread *thread) {
    const int sMaxRetries = 10000000;
    const int sMaxSpinCount = 25;

    int spinCount = 0;
    int retryCount = 0;

    while (thread->state != thread_running)
    {
        if (retryCount++ == sMaxRetries)
        {
            retryCount = 0;
            if(++spinCount >= sMaxSpinCount)
            {
                return -255; // give up
            } else if(thread->state != thread_running)
                return 0;
        }
    }
    return 0;
}

void Thread::suspendAllThreads() {
    Thread* thread;
    for(unsigned int i= 0; i < tp; i++) {
        thread=threads[i];

        if(thread!=NULL &&
                (thread->id != thread_self->id)){
            suspendThread(thread);
            waitForThreadSuspend(thread);
        }
    }
}

void Thread::resumeAllThreads() {
    Thread* thread;
    for(unsigned int i= 0; i < tp; i++) {
        thread=threads[i];

        if(thread!=NULL &&
           (thread->id != thread_self->id)){
            unsuspendThread(thread);
        }
    }
}

int Thread::unsuspendThread(Thread *thread) {
    thread->suspended = false;
    return 0;
}

void Thread::suspendThread(Thread *thread) {
    if(thread->id == thread_self->id)
        suspendSelf();
    else {
        thread->suspendPending = true;
    }
}

void Thread::term() {
    this->monitor.unlock();
    if(__stack != NULL)
        GC::_insert_stack(__stack, STACK_SIZE);
    this->name.free();
}

int Thread::join(int32_t id) {
    if (id == thread_self->id)
        return 1;

    Thread* thread = getThread(id);
    if (thread == NULL || thread->daemon)
        return 1;

    return threadjoin(thread);
}

int Thread::threadjoin(Thread *thread) {
    if (thread->state == thread_running)
    {
#ifdef WIN32_
        WaitForSingleObject(thread->thread, INFINITE);
        return 0;
#endif
#ifdef POSIX_
        if(pthread_join(thread->thread, NULL))
            return 3;
#endif
    }

    return 2;
}

void Thread::killAll() {
    Thread* thread;
    suspendAllThreads();

    for(unsigned int i = 0; i < tp; i++) {
        thread = threads[i];

        if(thread != NULL && thread->id != thread_self->id) {
            if(thread->state == thread_running){
                interrupt(thread);
                waitForThreadExit(thread);
            }

            thread->term();
        } else if(thread != NULL){
            thread->term();
        }
    }
}

int Thread::interrupt(Thread *thread) {
    if (thread->state == thread_running)
    {
        if (thread->id == main_threadid)
        {
            /*
            * Shutdown all running threads
            * and de-allocate all allocated
            * memory. If we do not call join()
            * to wait for all other threads
            * regardless of what they are doing, we
            * stop them.
            */
            vm->Shutdown();
        }
        else
        {
            thread->state = thread_killed; // terminate thread
            return 0;
        }
    }

    return 2;
}

void Thread::shutdown() {
    if(threads != NULL) {
        Thread::killAll();

        for(unsigned int i = 0; i < tp; i++) {
            if(threads[i] != NULL) {
                std::free(threads[i]); threads[i] = NULL;
            }
        }

        std::free (threads);
    }
}

void Thread::exit() {
    try {
        this->exitVal = 0;//(int)this->stack.pop()->obj->prim;
    } catch (Exception) {
        this->exitVal = 203;
    }

    if(this->exceptionThrown) {
        // TODO: handle exception
    }

    this->state = thread_killed;
    this->exited = true;
}

int Thread::startDaemon(
#ifdef WIN32_
        DWORD WINAPI
#endif
#ifdef POSIX_
void*
#endif
(*threadFunc)(void *), Thread *thread) {
    if (thread == NULL || !thread->daemon)
        return 1;

    if(thread->state == thread_running)
        return 2;

    thread->exited = false;
#ifdef WIN32_
    thread->thread = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size
            (LPTHREAD_START_ROUTINE)threadFunc,       // thread function caller
            thread,                 // thread self when thread is created
            0,                      // use default creation flags
            NULL);
    if(thread->thread == NULL) return 3; // thread was not started
    else
        return waitForThread(thread);
#endif
#ifdef POSIX_
    if(pthread_create( &thread->thread, NULL, threadFunc, (void*) thread)!=0)
        return 3; // thread was not started
    else
        return waitForThread(thread);
#endif
}

double exponent(int64_t n){
    if (n < 100000){
        // 5 or less
        if (n < 100){
            // 1 or 2
            if (n < 10)
                return n*0.1;
            else
                return n*0.01;
        }else{
            // 3 or 4 or 5
            if (n < 1000)
                return n*0.001;
            else{
                // 4 or 5
                if (n < 10000)
                    return n*0.0001;
                else
                    return n*0.00001;
            }
        }
    } else {
        // 6 or more
        if (n < 10000000) {
            // 6 or 7
            if (n < 1000000)
                return n*0.000001;
            else
                return n*0.0000001;
        } else if(n < 1000000000) {
            // 8 to 10
            if (n < 100000000)
                return n*0.00000001;
            else {
                // 9 or 10
                if (n < 1000000000)
                    return n*0.000000001;
                else
                    return n*0.0000000001;
            }
        } else if(n < 1000000000000000) {
            // 11 to 15
            if (n < 100000000000)
                return n*0.00000000001;
            else {
                // 12 to 15
                if (n < 1000000000000)
                    return n*0.000000000001;
                else if (n < 10000000000000)
                    return n*0.0000000000001;
                else if (n < 100000000000000)
                    return n*0.00000000000001;
                else
                    return n*0.000000000000001;
            }
        }
        else {
            return n*0.0000000000000001;
        }
    }
}

void Thread::run() {
    thread_self = this;
    Sh_object *ptr=NULL; // ToDO: when ptr is derefrenced assign pointer to null pointer data struct in environment

    pc = 0;
    Environment::init(__stack, STACK_SIZE);

    call_asp(main->id);
    _init_opcode_table

    try {
        for (;;) {
            interp:
            if(suspendPending)
                Thread::suspendSelf();
            if(state == thread_killed)
                return;

            DISPATCH();
            _NOP:
            NOP
                    _INT:
            _int(GET_Da(cache[pc]))
            MOVI:
            movi(GET_Da(cache[pc]))
            RET:
            ret
                    HLT:
            hlt
                    NEWi: /* Requires register value */
            _newi(GET_Da(cache[pc]))
            CHECK_CAST:
            check_cast
                    MOV8:
            mov8(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            MOV16:
            mov16(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            MOV32:
            mov32(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            MOV64:
            mov64(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            PUSHR:
            pushr(GET_Da(cache[pc]))
            ADD:
            _add(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            SUB:
            _sub(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            MUL:
            _mul(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            DIV:
            _div(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            MOD:
            mod(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            POP:
            _pop
                    INC:
            inc(GET_Da(cache[pc]))
            DEC:
            dec(GET_Da(cache[pc]))
            MOVR:
            movr(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            MOVX: /* Requires register value */
            movx(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            LT:
            lt(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            BRH:
            brh
                    BRE:
            bre
                    IFE:
            ife
                    IFNE:
            ifne
                    GT:
            gt(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            GTE:
            gte(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            LTE:
            lte(GET_Ca(cache[pc]),GET_Cb(cache[pc]))
            MOVL:
            movl(GET_Da(cache[pc]))
            OBJECT_NXT:
            object_nxt
                    OBJECT_PREV:
            object_prev
                    RMOV:
            _nativewrite2((int64_t)__rxs[GET_Ca(cache[pc])],__rxs[GET_Cb(cache[pc])]) _brh
                    MOV:
            _nativewrite3((int64_t)__rxs[GET_Ca(cache[pc])],GET_Cb(cache[pc])) _brh
                    MOVD:
            _nativewrite2((int64_t)__rxs[GET_Ca(cache[pc])],GET_Cb(cache[pc])) _brh
                    MOVBI:
            movbi(GET_Da(cache[pc]) + exponent(cache[pc+1]))
            _SIZEOF:
            _sizeof(GET_Da(cache[pc]))
            PUT:
            _put(GET_Da(cache[pc]))
            PUTC:
            putc(GET_Da(cache[pc]))
            CHECKLEN:
            _checklen(GET_Da(cache[pc]))
            GOTO:
                _goto(GET_Da(cache[pc]))
            LOADX:
                _loadx(GET_Da(cache[pc]))
            NEWstr:
                _newstr(GET_Da(cache[pc]))
            PUSHREF:
                pushref(ptr)
            DELREF:
                delref(ptr)
            INIT_FRAME:
                _init_frame()
            CALL:
                call(GET_Da(cache[pc]))
            NEW_CLASS:
                new_class(GET_Da(cache[pc]))
            MOVN:
                movn(GET_Da(cache[pc]))

        }
    } catch (std::bad_alloc &e) {
        // TODO: throw out of memory error
    } catch (Exception &e) {
        throwable = e.getThrowable();
        exceptionThrown = true;

        // TODO: handle exception
    }
}



#ifdef  DEBUGGING
int64_t getop(int64_t i) {
    return (i & OPCODE_MASK);
}
int64_t get_da(int64_t i){
    return (i >> 8);
}
int64_t get_ca(int64_t i) {
    return (((i >> 8) & 1) ? (-1*(i >> 9 & 0x7FFFFFF)) : (i >> 9 & 0x7FFFFFF));
}
int64_t get_cb(int64_t i) {
    return (i >> 36);
}
#endif


void Thread::call_asp(int64_t id) {
    if(id < 0 || id >= manifest.addresses) {
        stringstream ss;
        ss << "could not call method @" << id << "; method not found.";
        throw Exception(ss.str());
    }

    sh_asp* asp = env->__address_spaces+id;

    /*
     * Do we have enough space to allocate this new frame?
     */
    if((sp+(asp->frame_init-asp->param_size)+asp->self) < STACK_SIZE) {
        this->curr_adsp = asp->id;
        this->cache = asp->bytecode;

        fp= ((sp+1)-asp->param_size)-asp->self;
        sp = asp->frame_init == 0 ? fp : fp+(asp->frame_init-1);
        if(fp != 0) __stack[fp-pc_offset].var = pc; // reset pc to call address
        pc = 0;
    } else {
        // stack overflow err
    }
}

void Thread::init_frame() {
    int64_t old_sp = sp, frame_alloc = 4;

    /*
     * Do we have enough space to allocate this new frame?
     */
    if(sp+frame_alloc < STACK_SIZE) {
        __stack[++sp].var = old_sp; // store sp
        __stack[++sp].var = fp; // store frame pointer
        ++sp; // store pc
        __stack[++sp].var = curr_adsp; // store address_space id
    } else {
        // stack overflow err
    }
}

void Thread::return_asp() {
    int64_t id = (int64_t )__stack[fp-1].var;
    if(id < 0 || id >= manifest.addresses) {
        stringstream ss;
        ss << "could not return from method @" << id << "; method not found.";
        throw Exception(ss.str());
    }

    sh_asp* asp = &env->__address_spaces[id];
    curr_adsp = asp->id;
    cache = asp->bytecode;
    pc = (int64_t )__stack[fp-pc_offset].var;
    sp = (int64_t )__stack[fp-sp_offset].var;
    fp = (int64_t )__stack[fp-fp_offset].var;
}

void __os_sleep(int64_t INTERVAL) {
#ifdef WIN32_
    Sleep(INTERVAL);
#endif
#ifdef POSIX_
    usleep(INTERVAL);
#endif
}
