/*
    File: NetworkRequestChannel.C

    Author: R. Bettati
            Department of Computer Science
            Texas A&M University
    Date  : 2012/07/11

*/

/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

    /* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*--------------------------------------------------------------------------*/

#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>

#include "netreqchannel.h"

using namespace std;

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */ 
/*--------------------------------------------------------------------------*/

    /* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

    /* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* FORWARDS */
/*--------------------------------------------------------------------------*/

    /* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* PRIVATE METHODS FOR CLASS   R e q u e s t C h a n n e l  */
/*--------------------------------------------------------------------------*/



char * NetworkRequestChannel::pipe_name(Mode _mode) {
  string pname = "fifo_" + my_name;

  if (my_side == CLIENT_SIDE) {
    if (_mode == READ_MODE) 
      pname += "a";
    else
      pname += "b";
  } else {
    /* SERVER_SIDE */
    if (_mode == READ_MODE) 
      pname += "b";
    else 
      pname += "a";
  }
  char * result = new char[pname.size()+1];
  strncpy(result, pname.c_str(), pname.size()+1);
  return result;
}

void NetworkRequestChannel::open_write_pipe(char * _pipe_name) {

    //cout << "mkfifo write pipe [" << _pipe_name << "]\n" << flush;

  if (mkfifo(_pipe_name, 0600) < 0) {
    if (errno != EEXIST) {
      perror("Error creating pipe for writing; exit program");
      exit(1);
    }
  }

   //cout << "open write pipe [" << _pipe_name << "]\n" << flush;

  wfd = open(_pipe_name, O_WRONLY);
  if (wfd < 0) {
    perror("Error opening pipe for writing; exit program");
    exit(1);
  }

    //cout << "done opening write pipe [" << _pipe_name << "]\n" << flush;

}

void NetworkRequestChannel::open_read_pipe(char * _pipe_name) {

    //cout << "mkfifo read pipe [" << _pipe_name << "]\n" << flush;

  if (mkfifo(_pipe_name, 0600) < 0) {
    if (errno != EEXIST) {
      perror("Error creating pipe for writing; exit program");
      exit(1);
    }
  }

    //cout << "open read pipe [" << _pipe_name << "]\n" << flush;

  rfd = open(_pipe_name, O_RDONLY);
  if (rfd < 0) {
    perror("Error opening pipe for reading; exit program");
    exit(1);
  }

    //cout << "done opening read pipe [" << _pipe_name << "]\n" << flush;

}

/*--------------------------------------------------------------------------*/
/* CONSTRUCTOR/DESTRUCTOR FOR CLASS   R e q u e s t C h a n n e l  */
/*--------------------------------------------------------------------------*/

NetworkRequestChannel::NetworkRequestChannel(const string _server_host_name, const unsigned short _port_no) {

    
    struct addrinfo hints, *res;
    int sockfd;
    
    // first, load up address structs with getaddrinfo():
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int status;

    if ((status = getaddrinfo(_server_host_name, _port_no, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }
    
    // make a socket:
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0)
    {
        perror ("Error creating socket\n");
        return -1;
    }
    
    // connect!
    if (connect(sockfd, res->ai_addr, res->ai_addrlen)<0)
    {
        perror ("connect error\n");
        return -1;
    }

}

NetworkRequestChannel::NetworkRequestChannel(const unsigned short _port_no, void * (*connection_handler)(int *)) {
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *serv;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    int rv;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    if ((rv = getaddrinfo(NULL, port, &hints, &serv)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    if ((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1) {
        perror("server: socket");
        return -1;
    }
    if (bind(sockfd, _port_no, serv->ai_addrlen) == -1) {
        close(sockfd);
        perror("server: bind");
        return -1;
    }
    freeaddrinfo(serv); // all done with this structure

    if (listen(sockfd, 20) == -1) {
        perror("listen");
        exit(1);
    }
    
    while (true) {
        pthread_t tid;
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        int *ptr = new int(new_fd);
        pthread_create (&tid, 0, connection_handler, ptr);
    }
    
}


NetworkRequestChannel::~NetworkRequestChannel() {
  //cout << "close requests channel " << my_name << endl;
  close(wfd);
  close(rfd);
  if (my_side == SERVER_SIDE) {
    //cout << "close IPC mechanisms on server side for channel " << my_name << endl;
    /* Destruct the underlying IPC mechanisms. */
    if (remove(pipe_name(READ_MODE)) != 0) {
      perror(string("Request Channel (" + my_name + ") : Error deleting pipe for reading").c_str());
    }
      
    if (remove(pipe_name(WRITE_MODE)) != 0) {
      perror(string("Request Channel (" + my_name + ") : Error deleting pipe for writing").c_str());
    }
      //cout << "Bye y'all" << flush << endl;
  }
}

/*--------------------------------------------------------------------------*/
/* READ/WRITE FROM/TO REQUEST CHANNELS  */
/*--------------------------------------------------------------------------*/

const int MAX_MESSAGE = 255;

string NetworkRequestChannel::send_request(string _request) {
  cwrite(_request);
  string s = cread();
  return s;
}

string NetworkRequestChannel::cread() {

  char buf[MAX_MESSAGE];

  if (read(rfd, buf, MAX_MESSAGE) < 0) {
    perror(string("Request Channel (" + my_name + "): Error reading from pipe!").c_str());
  }
  
  string s = buf;

  //  cout << "Request Channel (" << my_name << ") reads [" << buf << "]\n";

  return s;

}

int NetworkRequestChannel::cwrite(string _msg) {

  if (_msg.length() >= MAX_MESSAGE) {
    cerr << "Message too long for Channel!\n";
    return -1;
  }

  //  cout << "Request Channel (" << my_name << ") writing [" << _msg << "]";

  const char * s = _msg.c_str();

  if (write(wfd, s, strlen(s)+1) < 0) {
    perror(string("Request Channel (" + my_name + ") : Error writing to pipe!").c_str());
  }

  //  cout << "(" << my_name << ") done writing." << endl;
}

/*--------------------------------------------------------------------------*/
/* ACCESS THE NAME OF REQUEST CHANNEL  */
/*--------------------------------------------------------------------------*/

string NetworkRequestChannel::name() {
  return my_name;
}

/*--------------------------------------------------------------------------*/
/* ACCESS FILE DESCRIPTORS OF REQUEST CHANNEL  */
/*--------------------------------------------------------------------------*/

int NetworkRequestChannel::read_fd() {
  return rfd;
}

int NetworkRequestChannel::write_fd() {
  return wfd;
}



