//
// Created by bknun on 1/18/2018.
//

#ifndef SHARP_KEYPAIR_H
#define SHARP_KEYPAIR_H

template <class K, class T>
struct KeyPair
{
    KeyPair(K k, T v)
            :
            key(k),
            value(v)
    {
    }

    KeyPair()
    {
    }
    void set(K k, T val)
    {
        key = k;
        value = val;
    }

    K key;
    T value;
};

#endif //SHARP_KEYPAIR_H