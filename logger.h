#ifndef LOGGER_H
#define LOGGER_H

#include <windows.h>
#include <stdio.h>

// Constants
#define NUM_THREADS 10
#define MAX_LOG_ENTRIES 1000000
#define LOG_FILE_PATH "logs\\log.txt"
#define LOG_MESSAGE_SIZE 256
#define BUFFERED_WRITE_SIZE 4096

// LogNode structure
typedef struct LogNode {
    char message[LOG_MESSAGE_SIZE];
    struct LogNode* next;
} LogNode;

// BlockingQueue structure
typedef struct {
    LogNode* head;
    LogNode* tail;
    int count;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE notEmpty;
    CONDITION_VARIABLE notFull;
    int capacity;
} BlockingQueue;

// Global variables declaration
extern BlockingQueue logQueue;
extern int isRunning;
extern int logCounter;
extern FILETIME startTime;
extern HANDLE logWriterThread;

// Function declarations
void initialize_queue(BlockingQueue* queue, int capacity);
void destroy_queue(BlockingQueue* queue);
void enqueue(BlockingQueue* queue, const char* message);
LogNode* dequeue(BlockingQueue* queue);
void produce_log_message(BlockingQueue* queue, int threadNumber, FILETIME startTime);
DWORD WINAPI log_producer(LPVOID arg);
DWORD WINAPI log_writer(LPVOID arg);
void GetElapsedTime(FILETIME startTime, double* elapsedSeconds);

#endif // LOGGER_H
