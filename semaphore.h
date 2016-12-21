#pragma once

#include <pthread.h>

class semaphore {
private:
    int counter = 0;
    pthread_mutex_t semaphore_lock;
    pthread_cond_t wait_queue;

public:
    semaphore (int counter) { 
        this->counter = counter;
        pthread_mutex_init(&semaphore_lock, NULL);
        pthread_cond_init(&wait_queue, NULL);
    }
    ~semaphore () {
        pthread_mutex_destroy(&semaphore_lock);
        pthread_cond_destroy(&wait_queue);
    }
    void P() {
        pthread_mutex_lock(&semaphore_lock);
        --counter;
        if (counter < 0) {
            pthread_cond_wait(&wait_queue, &semaphore_lock);
        }
        pthread_mutex_unlock(&semaphore_lock);
        //printf("P-ed %d \n", counter);
    }
    void V() {

        pthread_mutex_lock(&semaphore_lock);
        ++counter;
        if (counter <= 0) {
        	pthread_cond_signal(&wait_queue);
        }
        pthread_mutex_unlock(&semaphore_lock);
        //printf("V-ed %d \n", counter);

    }
};