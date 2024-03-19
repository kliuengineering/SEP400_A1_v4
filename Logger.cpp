// required libs
#include <iostream>
#include <unistd.h>
#include <atomic>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <time.h>
#include <queue>
#include <iterator>
#include "Logger.h"


// macroes
#define PORT_SERVER 8080
#define PORT_LOGGER 9090
#define SIZE_BUF 1024

const char ADDRESS_SERVER[] = "127.0.0.1";
const char ADDRESS_LOGGER[] = "127.0.0.1";

// connections - address, port, socket
struct sockaddr_in addr_server;
int fd_socket = -1;

// communication - message, buffer
char message[] = "";
char buffer[SIZE_BUF];

// log level - defaults to DEBUG mode
LOG_LEVEL log_level_current = DEBUG;

// use atomic to avoid race condition
std::atomic<bool> is_running = true;

// for mutex, we need to use thread methods but not std::mutex
pthread_mutex_t mutex_log; 
pthread_t recv_thread;





// utility function -> clear input stream
void ClearInputBuffer()
{
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');     // just ignore the rest
}


// utility function -> will set the filter log level and store in a variable global within Logger.cpp.
void SetLogLevel(LOG_LEVEL new_level)
{
    //pthread_mutex_lock(&mutex_log);
    log_level_current = new_level;
    //pthread_mutex_unlock(&mutex_log);
}


// receive thread function
void *ReceiveData(void *arg)
{
    // needs thread function to receive data
    // remembeer to implement non-blocking receive_from call here

    char buffer[SIZE_BUF];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (is_running)
    {
        // needs to implement receive_from logic with non-blocking mode handled by socket flags
        std::cout << "[DevLog] Location -> ReceiveData() -> while()\n";

        memset(buffer, 0, SIZE_BUF);
        ssize_t msg_len = recvfrom( fd_socket, buffer, SIZE_BUF, 0, (struct sockaddr *)&sender_addr, &sender_len );

        if(msg_len > 0)
        {
            std::cout << "[DevLog] Location -> ReceiveData() -> while() -> before mutex lock\n";

            pthread_mutex_lock(&mutex_log);

            std::cout << "[DevLog] Location -> ReceiveData() -> while() -> after mutex lock\n";

            // let's attempt to parse the received message as a log level command now
            int new_level;
            if ( sscanf(buffer, "Set Log Level=%d", &new_level) == 1 )
            {

                std::cout << "[DevLog] Location -> ReceiveData() -> while() -> after mutex lock -> parsing incoming packet...\n";
                

                if (new_level >= DEBUG && new_level <= CRITICAL)
                {
                    std::cout << "[DevLog] Log level current is -> " << log_level_current << std::endl;
                    
                    // log_level_current = static_cast<LOG_LEVEL>(new_level);
                    SetLogLevel(static_cast<LOG_LEVEL>(new_level));

                    std::cout << "Log level updated to " << new_level << std::endl;
                    std::cout << "[DevLog] Log level current is -> " << log_level_current << std::endl;
                }
                else
                {
                    std::cout << "Received invalid log level. " << new_level << std::endl;
                }
            }

            pthread_mutex_unlock(&mutex_log);
        }
        else
        {
            sleep(1);
        }
    }

    return nullptr;
}


/*
create a non-blocking socket for UDP communications (AF_INET, SOCK_DGRAM).
Set the address and port of the server.
Create a mutex to protect any shared resources.
Start the receive thread and pass the file descriptor to it.
*/
int InitializeLog(void)
{
    // creates fd for UDP
    // needs to add sock_nonblock? 
    if ((fd_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("\nERROR: socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // socket -> non blocking
    int flag = fcntl(fd_socket, F_GETFL, 0);
    if(flag < 0)
    {
        perror("\nERROR: socket flag non blocking issue\n");
        exit(EXIT_FAILURE);
    }

    // if non blocking -> success
    flag |= O_NONBLOCK;
    if (fcntl(fd_socket, F_SETFL, flag) < 0)
    {
        perror("\nERROR: setting socket non blocking");
        exit(EXIT_FAILURE);
    }

    // upon success, then
    memset(&addr_server, 0, sizeof(addr_server));

    // configures the server info -> ipv4, port#, ip
    addr_server.sin_family = AF_INET;   
    addr_server.sin_port = htons(PORT_LOGGER);
    if ( inet_pton(AF_INET, ADDRESS_SERVER, &addr_server.sin_addr) <=0 )       // prototyping purposes -> ANY ip
    {
        perror("inet_pton failed...\n");
        exit(EXIT_FAILURE);
    }

    // bind()
    if ( bind( fd_socket, (struct sockaddr *)&addr_server, sizeof(addr_server) ) <0 )
    {
        perror("bind() failed.\n");
        exit(EXIT_FAILURE);
    }

    // initializes mutex
    if(pthread_mutex_init(&mutex_log, NULL) != 0)
    {
        perror("\nERROR: mutex init has failed\n");
        exit(EXIT_FAILURE);
    }

    // starts receiving thread
    if (pthread_create(&recv_thread, NULL, &ReceiveData, NULL) != 0)
    {
        perror("\nError: failed to create the receive thread\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}


/*
compare the severity of the log to the filter log severity. The log will be thrown away if its severity is lower than the filter log severity.
create a timestamp to be added to the log message. Code for creating the log message will look something like:
time_t now = time(0);
char *dt = ctime(&now);
memset(buf, 0, BUF_LEN);
char levelStr[][16]={"DEBUG", "WARNING", "ERROR", "CRITICAL"};
len = sprintf(buf, "%s %s %s:%s:%d %s\n", dt, levelStr[level], file, func, line, message)+1;
buf[len-1]='\0';
apply mutexing to any shared resources used within the Log() function.
The message will be sent to the server via UDP sendto().
*/
void Log(LOG_LEVEL level, const char *prog, const char *func, int line, const char *message)
{
    // discard message if it is lower than the current threshold
    if (level < log_level_current)  return;

    // stages the message
    time_t now = time(nullptr);
    struct tm *tm_info;
    char dt[30];     
    tm_info = localtime(&now);
    strftime(dt, 30, "%Y-%m-%d %H:%M:%S", tm_info);     // controlled formatting

    char buffer_local[SIZE_BUF];
    memset(buffer_local, 0, SIZE_BUF);

    const char *level_str[] = {"DEBUG", "WARNING", "ERROR", "CRITICAL"};
    int len = snprintf(buffer_local, SIZE_BUF, "%s %s %s: %s: %d %s\n", dt, level_str[level], prog, func, line, message) + 1;
    buffer_local[SIZE_BUF-1] = '\0';
    
    if (len < 0 || len>= SIZE_BUF) {}       // handles snprintf error, etc.

    // mutexes the critical section
    pthread_mutex_lock(&mutex_log);
    {
        if (sendto(fd_socket, buffer_local, strlen(buffer_local), 0, (struct sockaddr *)&addr_server, sizeof(addr_server)) < 0)
        {
            perror("Error sending stuff to the server...\n");
            exit(EXIT_FAILURE);
        }
    }
    pthread_mutex_unlock(&mutex_log);
}


// will stop the receive thread via an is_running flag and close the file descriptor.
void ExitLog(void)
{
    is_running = false;

    // join the receive thread
    if ( pthread_join(recv_thread, NULL) !=0 )
    {
        perror("Failed to join receive thread.\n");
    }

    pthread_mutex_destroy(&mutex_log);

    close(fd_socket);
}