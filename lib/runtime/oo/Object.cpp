//
// Created by BraxtonN on 2/12/2018.
//

#include "Object.h"
#include "../memory/GarbageCollector.h"
#include "../Environment.h"
#include "../Thread.h"

void Object::castObject(uint64_t classPtr) {
    if(this->object == NULL)
        throw Exception(Environment::ClassCastException, "invalid cast on null object");
    if(this->object->k == NULL)
        throw Exception(Environment::ClassCastException, "invalid cast on non-class object");

    ClassObject* k = env->findClassBySerial(classPtr);
    if(!k->hasBaseClass(this->object->k)) {
        stringstream ss;
        ss << "illegal cast of class '" << this->object->k->name.str() << "' and '";
        ss << k->name.str() << "'";
        throw Exception(Environment::ClassCastException, ss.str());
    }
}
