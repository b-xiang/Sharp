//
// Created by BraxtonN on 2/12/2018.
//

#include "Object.h"
#include "../memory/GarbageCollector.h"
#include "../Environment.h"
#include "../register.h"
#include "../Thread.h"

void Object::castObject(uint64_t classPtr) {
    ClassObject* k = env->findClassBySerial(classPtr);

    stringstream nonclass;
    nonclass << "attempt to perform invalid cast to [" << k->name.str() << "] on non-class object ";

    if(this->object == NULL)
        throw Exception(Environment::ClassCastException, "invalid cast on null object");
    if(this->object->k == NULL)
        throw Exception(Environment::ClassCastException, nonclass.str());

    if(k->serial!= this->object->k->serial && !this->object->k->hasBaseClass(k)) {
        // validate we have all our interfaces checked
        for(int i = 0; i < this->object->k->interfaceCount; i++) {
            ClassObject* _interface = env->findClassBySerial(this->object->k->interfaces[i]);
            if(_interface->serial==k->serial || _interface->hasBaseClass(k))
                return;
        }

        stringstream ss;
        ss << "illegal cast of class '" << this->object->k->name.str() << "' to '";
        ss << k->name.str() << "'";
        throw Exception(Environment::ClassCastException, ss.str());
    }
}
