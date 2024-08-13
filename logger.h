#ifndef LOGGER_H
#define LOGGER_H

#include <windows.h>
#include <stdio.h>

#define NUM_THREADS 10
#define MAX_LOG_ENTRIES 1000000
#define LOG_FILE_PATH "logs\\log.txt"
#define LOG_MESSAGE_SIZE 256
#define BUFFERED_WRITE_SIZE 4096

typedef struct LogNode {
    char message[LOG_MESSAGE_SIZE];
    struct LogNode* next;
} LogNode;

typedef struct {
    LogNode* head;
    LogNode* tail;
    int count;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE notEmpty;
    CONDITION_VARIABLE notFull;
    int capacity;
} BlockingQueue;

typedef struct{
    BlockingQueue logQueue;
    int isRunning;
    int logCounter;
    FILETIME startTime;
    HANDLE logWriterThread;
    HANDLE loggingThreads[NUM_THREADS];
    FILE* logFile;
} LoggerContext;

void initialize_logger(LoggerContext* logger, int capacity);
void start_logging(LoggerContext* logger);
void stop_logging(LoggerContext* logger);
void GetElapsedTime(FILETIME startTime, double* elapsedSeconds);

#endif // LOGGER_H
