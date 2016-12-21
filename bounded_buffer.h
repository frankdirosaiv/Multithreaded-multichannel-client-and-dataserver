#pragma once

#include <queue>
#include <string>
#include "semaphore.h"

class bounded_buffer {
private:
    semaphore* full;
    semaphore* empty;
    std::queue<std::string> buffer;
    pthread_mutex_t buffer_lock;
public:
    bounded_buffer(int boundary) {
        pthread_mutex_init(&buffer_lock, NULL);
        full = new semaphore(0);
        empty = new semaphore(boundary);
    }
    ~bounded_buffer() {
        pthread_mutex_destroy(&buffer_lock);
        delete full;
        delete empty;
    }
    void push_back(std::string data) {
        empty->P();
        pthread_mutex_lock(&buffer_lock);
        buffer.push(data);
        pthread_mutex_unlock(&buffer_lock);
        full->V();
    }

    std::string pop_front() {
        full->P();
        pthread_mutex_lock(&buffer_lock);
        std::string temp = buffer.front();
        buffer.pop();
        pthread_mutex_unlock(&buffer_lock);
        empty->V();
        return temp;
    }
    
    bool empty1(){
        return buffer.empty();
    }

};
