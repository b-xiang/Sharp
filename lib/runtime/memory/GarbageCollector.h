//
// Created by BraxtonN on 2/11/2018.
//

#ifndef SHARP_GARBAGECOLLECTOR_H
#define SHARP_GARBAGECOLLECTOR_H

#include "../List.h"

enum CollectionPolicy
{
    GC_LOW          =0x001,
    GC_CONCURRENT   =0x002,
    GC_EXPLICIT     =0x003
};

enum CollectionGeneration
{
    gc_young = 0,
    gc_adult = 1,
    gc_old   = 2,

    /**
     * This is a permanent generation. objects promoted to this generation will live
     * for the entirety of the program and will not be garbage collected until the end of
     * the program. Objects in this state must be set manually
     */
    gc_perm  = 3
};

struct Object;
struct SharpObject;
class ClassObject;

#define heap (*_Mheap)

class GarbageCollector {
public:
    static GarbageCollector *self;
    recursive_mutex mutex;
    List<CollectionPolicy> messageQueue;
    uint64_t x;

    static void initilize();
    static void startup();
    void shutdown();
    static
#ifdef WIN32_
    DWORD WINAPI
#endif
#ifdef POSIX_
    void*
#endif
    threadStart(void *);
    void run();

    static void setMemoryLimit(unsigned long limit) {
        if(self != NULL) {
            self->memoryLimit = limit;
        }
    }

    /**
     * Messages are sendable by other threads to command the garbage collector
     * to preform a certain type of garbage collection. Most of the time the user
     * will not have to do this
     * @param message
     */
    void sendMessage(CollectionPolicy message);

    /**
     * @Policy: GC_LOW
     * The garbage collector will stop all threads to deallocate memory
     * this will only happen if the physical memory on the computer is running low
     * during allocation
     *
     * @Policy: GC_EXPLICIT
     * Explicit collection always will happen on the thread to which its called upon
     *
     * @Policy: GC_CONCURRENT
     * Concurrent collections will occur as the GC
     */
    void collect(CollectionPolicy policy);

    SharpObject* newObject(unsigned long size); /* Array allocation */
    SharpObject* newObject(ClassObject* k); /* Class allocation */

    SharpObject* newObjectArray(unsigned long size); /* Array Object allocation */
    SharpObject* newObjectArray(unsigned long size, ClassObject* k); /* Class Array allocation */

    void createStringArray(Object* object, native_string& s); /* Native string allocation */
    void createStringArray(native_string &s);

    /**
     * Function call by virtual machine
     * @param object
     */
     void freeObject(Object* object);

    CXX11_INLINE bool spaceAvailable(size_t bytes) {
        return (bytes+managedBytes) < memoryLimit;
    }

    /**
     * This will keep track of our different generations and the
     * objects that are living in them.
     *
     * @youngObjects: This will hold the total running count of objects
     * living in that generation
     *
     * @yObjs: This is the amount of dropped objects in each generation since collection.
     * This does not mean that if there are 100 objects dropped that every one will be freed
     * its just an estimate
     */
    unsigned long objs;
private:
    unsigned long managedBytes;
    unsigned long memoryLimit;
    bool isShutdown;
    std::list<SharpObject*>::iterator iterator;

    /**
     * This will keep track of our different generations and the
     * objects that are living in them.
     *
     * @youngObjects: This will hold the total running count of objects
     * living in that generation
     *
     * @yObjs: This is the amount of dropped objects in each generation since collection.
     * This does not mean that if there are 100 objects dropped that every one will be freed
     * its just an estimate
     */
    /* collect when 10% has been dropped */
    //unsigned long youngObjects;
    /* collect when 40% has been dropped */
    //unsigned long adultObjects;
    /* collect when 20% has been dropped */
    unsigned long liveObjects;
    //unsigned long x;
    std::list<SharpObject*>* _Mheap;

    void collectYoungObjects();
    void collectAdultObjects();
    void collectOldObjects();

    /**
     * This function performs the actual collection of
     * Sharp objects in the heap
     * @param object
     * @return
     */
    void sweep(SharpObject *object);

    void markObject(SharpObject *object);

    CXX11_INLINE void invalidate(SharpObject *object) {
        for (auto it = heap.begin(); it != heap.end();) {
            if(*it == object) {
                iterator = heap.erase(it);
                return;
            } else
                ++it;
        }
    }
};

#define GC_COLLECT() ( (unsigned int)(((double)objs/(double)liveObjects)*100) >= 10 )
#define GC_COLLECT_YOUNG() ( (unsigned int)(((double)yObjs/(double)youngObjects)*100) >= 10 )
#define GC_COLLECT_ADULT() ( (unsigned int)(((double)aObjs/(double)adultObjects)*100) >= 40 )
#define GC_COLLECT_OLD() ( (unsigned int)(((double)oObjs/(double)oldObjects)*100) >= 20 )
#define GC_HEAP_LIMIT (GB_TO_BYTES(1))

#define IS_MARKED(g) (g == 0x1)

#define UPDATE_GC(object) \
    liveObjects--;

/**
 * Bytes are used via the JEDEC Standard 100B.01
 */
#define KB_TO_BYTES(bytes) (((unsigned long)bytes)*1024)
#define MB_TO_BYTES(bytes) (((unsigned long)bytes)*1048576)
#define GB_TO_BYTES(bytes) (((unsigned long)bytes)*1073741824)


#endif //SHARP_GARBAGECOLLECTOR_H
