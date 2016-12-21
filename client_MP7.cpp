/*
    File: client_MP6.cpp

    Author: J. Higginbotham
    Department of Computer Science
    Texas A&M University
    Date  : 2016/05/21

    Based on original code by: Dr. R. Bettati, PhD
    Department of Computer Science
    Texas A&M University
    Date  : 2013/01/31

    MP6 for Dr. //Tyagi's
    Ahmed's sections of CSCE 313.
 */

/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

    /* -- (none) -- */
    /* -- This might be a good place to put the size of
        of the patient response buffers -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*
    As in MP7 no additional includes are required
    to complete the assignment, but you're welcome to use
    any that you think would help.
*/
/*--------------------------------------------------------------------------*/

#include <cassert> 
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <string>
#include <sstream>
#include <sys/time.h>
#include <assert.h>
#include <fstream>
#include <numeric>
#include <vector>
#include <queue>
#include <algorithm>
#include "reqchannel.h"

/*
    This next file will need to be written from scratch, along with
    semaphore.h and (if you choose) their corresponding .cpp files.
 */

#include "bounded_buffer.h"
#include "semaphore.h"

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */
/*--------------------------------------------------------------------------*/


/*
    All *_params structs are optional,
    but they might help.
 */

//Request_thread_params
struct request_thread_params {
    bounded_buffer* buffer;
    std::string people;
    int* n;
    request_thread_params(bounded_buffer* buffer, std::string people, int* n) {
        this->buffer = buffer;
        this->people = people;
        this->n = n;
    }
};

//Event_handler_params
struct event_handler_params {
    bounded_buffer* buffer;
    bounded_buffer* John_buffer;
    bounded_buffer* Jane_buffer;
    bounded_buffer* Joe_buffer;
    int w;
    int count;
    RequestChannel* channel;
    event_handler_params(bounded_buffer* John_buffer,
        bounded_buffer* Jane_buffer, 
        bounded_buffer* Joe_buffer, 
        bounded_buffer* buffer, int w, int count) {
        this->John_buffer = John_buffer;
        this->Jane_buffer = Jane_buffer;
        this->Joe_buffer = Joe_buffer;
        this->buffer = buffer;
        this->w = w;
        this->count = count;
    }
};

//Stat_thread_params
struct stat_thread_params {
    bounded_buffer* buffer;
    std::vector<int>* frequency_count;
    pthread_mutex_t stat_lock;
    int count;
    stat_thread_params(std::vector<int>* frequency_count, bounded_buffer* buffer, int count){
        this->frequency_count = frequency_count;
        this->buffer = buffer;
        this->count = count;
        pthread_mutex_init(&stat_lock, NULL);
    }
    ~stat_thread_params() {
        pthread_mutex_destroy(&stat_lock);
    }
};

/*
    This class can be used to write to standard output
    in a multithreaded environment. It's primary purpose
    is printing debug messages while multiple threads
    are in execution.
 */
class atomic_standard_output {
    pthread_mutex_t console_lock;
public:
    atomic_standard_output() { pthread_mutex_init(&console_lock, NULL); }
    ~atomic_standard_output() { pthread_mutex_destroy(&console_lock); }
    void print(std::string s){
        pthread_mutex_lock(&console_lock);
        std::cout << s << std::endl;
        pthread_mutex_unlock(&console_lock);
    }
};

atomic_standard_output threadsafe_standard_output;

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

    /* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* HELPER FUNCTIONS */
/*--------------------------------------------------------------------------*/

std::string make_histogram(std::string name, std::vector<int> *data) {
    std::string results = "Frequency count for " + name + ":\n";
    for(int i = 0; i < data->size(); ++i) {
        results += std::to_string(i * 10) + "-" + std::to_string((i * 10) + 9) + ": " + std::to_string(data->at(i)) + "\n";
    }
    return results;
}

//Request_thread_function
void* request_thread_function(void* arg) {
    request_thread_params* params = (request_thread_params*)arg;
    fflush(NULL);
    for (int i = 0; i < *(params->n); ++i) {
        params->buffer->push_back(params->people);
    }
}

//Event_handler_function
void* event_handler_function(void* arg) {
    event_handler_params* p = (event_handler_params*)arg;
    std::vector<RequestChannel*> channels;
    std::vector<std::string> match (p->w, " ");
    fd_set rfds;
    int retval;
    int initial = 0;
    //Initialze all the Request Channels
    for(int i = 0; i < p->w; ++i){
        std::string s = p->channel->send_request("newthread");
        RequestChannel *workerChannel = new RequestChannel(s, RequestChannel::CLIENT_SIDE);
        channels.push_back(workerChannel);
    }
    
    //Send the intial writes off
    while(initial < std::min(p->w, p->count)){
        std::string request = p->buffer->pop_front();
        channels[initial]->cwrite(request);
        match[initial] = request;
        ++initial;
    }
    
    //Keep looping until everything has been read and written
    while(1){
        //Initialize the set
        FD_ZERO(&rfds);
        for(int i = 0; i < p->w; ++i){
            FD_SET(channels[i]->read_fd(), &rfds);
        }
        int retval = select(channels[(p->w)-1]->read_fd()+1, &rfds, NULL, NULL, NULL);
        
        //Check to see which ones are available
        for(int i = 0; i < p->w; ++i){
            if(FD_ISSET(channels[i]->read_fd(), &rfds)){
                string s = channels[i]->cread();
                --(p->count);
                if(match[i] == "data John Smith") {
                    p->John_buffer->push_back(s);
                }
                else if(match[i] == "data Jane Smith") {
                    p->Jane_buffer->push_back(s);
                }
                else if(match[i] == "data Joe Smith") {
                    p->Joe_buffer->push_back(s);
                }
                //Send another request if there are requests left
                if(!p->buffer->empty1()){
                    std::string request = p->buffer->pop_front();
                    channels[i]->cwrite(request);
                    match[i] = request;
                }
            }
        }
        //Break when everything has been processed
        if(p->count <= 0){
            break;
        }
    }
    //Delete the worker channels
    for (int i = 0; i < p->w; ++i) {
        channels[i]->send_request("quit");
        delete channels[i];
    }
}

//Stat_thread_function
void* stat_thread_function(void* arg) {
    stat_thread_params* p = (stat_thread_params*)arg;
    while(p->count > 0) {
        pthread_mutex_lock(&p->stat_lock);
        std::string response = p->buffer->pop_front();
        p->count = p->count -1;
        p->frequency_count->at(stoi(response) / 10) += 1;
        pthread_mutex_unlock(&p->stat_lock);
    }
}

/*--------------------------------------------------------------------------*/
/* MAIN FUNCTION */
/*--------------------------------------------------------------------------*/

int main(int argc, char * argv[]) {
    int n = 10; //default number of requests per "patient"
    int b = 50; //default size of request_buffer
    int w = 10; //default number of worker threads
    bool USE_ALTERNATE_FILE_OUTPUT = false;
    int opt = 0;
    while ((opt = getopt(argc, argv, "n:b:w:m:h")) != -1) {
        switch (opt) {
            case 'n':
                n = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'm':
                if(atoi(optarg) == 2) USE_ALTERNATE_FILE_OUTPUT = true;
                break;
            case 'h':
            default:
                std::cout << "This program can be invoked with the following flags:" << std::endl;
                std::cout << "-n [int]: number of requests per patient" << std::endl;
                std::cout << "-b [int]: size of request buffer" << std::endl;
                std::cout << "-w [int]: number of worker threads" << std::endl;
                std::cout << "-m 2: use output2.txt instead of output.txt for all file output" << std::endl;
                std::cout << "-h: print this message and quit" << std::endl;
                std::cout << "Example: ./client_solution -n 10000 -b 50 -w 120 -m 2" << std::endl;
                std::cout << "If a given flag is not used, a default value will be given" << std::endl;
                std::cout << "to its corresponding variable. If an illegal option is detected," << std::endl;
                std::cout << "behavior is the same as using the -h flag." << std::endl;
                exit(0);
        }
    }

    int count = 3*n;
    bounded_buffer* buffer = new bounded_buffer(b);

    request_thread_params* params1 = new request_thread_params(buffer, "data John Smith", &n);
    request_thread_params* params2 = new request_thread_params(buffer, "data Jane Smith", &n);
    request_thread_params* params3 = new request_thread_params(buffer, "data Joe Smith", &n); 

    bounded_buffer* john_buffer = new bounded_buffer(n);
    bounded_buffer* jane_buffer = new bounded_buffer(n);
    bounded_buffer* joe_buffer = new bounded_buffer(n);

    std::vector<int>* john_frequency_count = new std::vector<int> (10, 0);
    std::vector<int>* jane_frequency_count = new std::vector<int> (10, 0);
    std::vector<int>* joe_frequency_count = new std::vector<int> (10, 0);

    stat_thread_params* stat_params_John = new stat_thread_params(john_frequency_count, john_buffer, n);
    stat_thread_params* stat_params_Jane = new stat_thread_params(jane_frequency_count, jane_buffer, n);
    stat_thread_params* stat_params_Joe = new stat_thread_params(joe_frequency_count, joe_buffer, n);

    event_handler_params* event_params = new event_handler_params(john_buffer, jane_buffer, joe_buffer, buffer, w, count);

    int pid = fork(); 
    if(pid == 0){
        struct timeval start_time;
        struct timeval finish_time;
        int64_t start_usecs;
        int64_t finish_usecs;
        ofstream ofs;
        if(USE_ALTERNATE_FILE_OUTPUT) ofs.open("output2.txt", ios::out | ios::app);
        else ofs.open("output.txt", ios::out | ios::app);
        
        std::cout << "n == " << n << std::endl;
        std::cout << "b == " << b << std::endl;
        std::cout << "w == " << w << std::endl;
        
        std::cout << "CLIENT STARTED:" << std::endl;
        std::cout << "Establishing control channel... " << std::flush;
        RequestChannel *chan = new RequestChannel("control", RequestChannel::CLIENT_SIDE);
        event_params->channel = chan;
        std::cout << "done." << std::endl;

        
        /*-------------------------------------------*/
        /* START TIMER HERE */
        /*-------------------------------------------*/

        gettimeofday (&start_time, NULL);
        
        /*
            This time you're up a creek.
            What goes in this section of the code?
            Hint: it looks a bit like what went here 
            in MP7, but only a *little* bit.
         */
            pthread_t thread[3]; 
            pthread_create( &(thread[0]), NULL, request_thread_function, (void *)params1);
            pthread_create( &(thread[1]), NULL, request_thread_function, (void *)params2);
            pthread_create( &(thread[2]), NULL, request_thread_function, (void *)params3);
        
            pthread_t event_hander_thread;
            pthread_create( &event_hander_thread, NULL, event_handler_function, (void *)event_params);
        
            pthread_t stat_thread[3]; 
            pthread_create( &(stat_thread[0]), NULL, stat_thread_function, (void *)stat_params_John);
            pthread_create( &(stat_thread[1]), NULL, stat_thread_function, (void *)stat_params_Jane);
            pthread_create( &(stat_thread[2]), NULL, stat_thread_function, (void *)stat_params_Joe);

            for (int i = 0; i < 3; ++i) {
                pthread_join(thread[i], NULL);
            }
        
            pthread_join(event_hander_thread, NULL);
        
            for (int i = 0; i < 3; ++i) {
                pthread_join(stat_thread[i], NULL);
            }
        
        ofs.close();

        /*-------------------------------------------*/
        /* END TIMER HERE   */
        /*-------------------------------------------*/
                gettimeofday (&finish_time, NULL);

        start_usecs = (start_time.tv_sec * 1e6) + start_time.tv_usec;
        finish_usecs = (finish_time.tv_sec * 1e6) + finish_time.tv_usec;
        std::cout << "Finished!" << std::endl;
        
        std::string john_results = make_histogram("John Smith", john_frequency_count);
        std::string jane_results = make_histogram("Jane Smith Smith", jane_frequency_count);
        std::string joe_results = make_histogram("Joe Smith", joe_frequency_count);
        
        std::cout << "Results for n == " << n << ", w == " << w << std::endl;
        std::cout << "Time to completion: " << std::to_string(finish_usecs - start_usecs) << " usecs" << std::endl;
        std::cout << "John Smith total: " << accumulate(john_frequency_count->begin(), john_frequency_count->end(), 0) << std::endl;
        std::cout << john_results << std::endl;
        std::cout << "Jane Smith total: " << accumulate(jane_frequency_count->begin(), jane_frequency_count->end(), 0) << std::endl;
        std::cout << jane_results << std::endl;
        std::cout << "Joe Smith total: " << accumulate(joe_frequency_count->begin(),joe_frequency_count->end(), 0) << std::endl;
        std::cout << joe_results << std::endl;

        std::cout << "Sleeping..." << std::endl;
        usleep(10000);
        
        std::string finale = chan->send_request("quit");
        std::cout << "Finale: " << finale << std::endl;

        //Free memory
        delete chan;
        delete buffer;
        delete params1;
        delete params2;
        delete params3;
        delete john_buffer;
        delete jane_buffer;
        delete joe_buffer;
        delete john_frequency_count;
        delete jane_frequency_count;
        delete joe_frequency_count;
        delete stat_params_John;
        delete stat_params_Jane;
        delete stat_params_Joe;
        delete event_params;
    }
    else if(pid != 0) execl("dataserver", NULL);
}
