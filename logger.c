#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BlockingQueue logQueue;
int isRunning = 1;
int logCounter = 0;
FILETIME startTime;
HANDLE logWriterThread;

// Implementations of the functions
void initialize_queue(BlockingQueue* queue, int capacity) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->capacity = capacity;
    InitializeCriticalSection(&queue->cs);
    InitializeConditionVariable(&queue->notEmpty);
    InitializeConditionVariable(&queue->notFull);
}

void destroy_queue(BlockingQueue* queue) {
    LogNode* current = queue->head;
    while (current != NULL) {
        LogNode* next = current->next;
        free(current);
        current = next;
    }
    DeleteCriticalSection(&queue->cs);
}

void enqueue(BlockingQueue* queue, const char* message) {
    LogNode* newNode = (LogNode*)malloc(sizeof(LogNode));
    if (newNode == NULL) {
        perror("Failed to allocate memory for log node");
        exit(EXIT_FAILURE);
    }
    strcpy_s(newNode->message, sizeof(newNode->message), message);
    newNode->next = NULL;

    EnterCriticalSection(&queue->cs);
    while (queue->count == queue->capacity) {
        SleepConditionVariableCS(&queue->notFull, &queue->cs, INFINITE);
    }

    if (queue->tail) {
        queue->tail->next = newNode;
    }
    else {
        queue->head = newNode;
    }
    queue->tail = newNode;
    queue->count++;

    LeaveCriticalSection(&queue->cs);
    WakeConditionVariable(&queue->notEmpty);
}

LogNode* dequeue(BlockingQueue* queue) {
    LogNode* node = queue->head;
    if (node) {
        queue->head = node->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
        queue->count--;
    }
    return node;
}

void produce_log_message(BlockingQueue* queue, int threadNumber, FILETIME startTime) {
    char logMessage[LOG_MESSAGE_SIZE];
    double elapsedSeconds;

    while (1) {
        EnterCriticalSection(&logQueue.cs);
        if (logCounter >= MAX_LOG_ENTRIES) {
            LeaveCriticalSection(&logQueue.cs);
            break;
        }
        logCounter++;
        LeaveCriticalSection(&logQueue.cs);

        GetElapsedTime(startTime, &elapsedSeconds);
        snprintf(logMessage, sizeof(logMessage), "%.6f | Thread %d | Logging message %d", elapsedSeconds, threadNumber, logCounter);
        logMessage[sizeof(logMessage) - 1] = '\0';

        enqueue(queue, logMessage);
    }
}

DWORD WINAPI log_producer(LPVOID arg) {
    int threadNumber = *(int*)arg;
    free(arg);
    produce_log_message(&logQueue, threadNumber, startTime);
    return 0;
}

DWORD WINAPI log_writer(LPVOID arg) {
    FILE* logFile = (FILE*)arg;

    char writeBuffer[BUFFERED_WRITE_SIZE];
    size_t bufferIndex = 0;

    while (1) {
        EnterCriticalSection(&logQueue.cs);
        while (logQueue.count == 0 && isRunning) {
            SleepConditionVariableCS(&logQueue.notEmpty, &logQueue.cs, INFINITE);
        }
        if (!isRunning && logQueue.count == 0) {
            LeaveCriticalSection(&logQueue.cs);
            break;
        }
        LogNode* node = dequeue(&logQueue);
        LeaveCriticalSection(&logQueue.cs);

        if (node) {
            size_t messageLength = strlen(node->message);
            if (bufferIndex + messageLength + 1 > BUFFERED_WRITE_SIZE) {
                fwrite(writeBuffer, 1, bufferIndex, logFile);
                bufferIndex = 0;
            }
            memcpy(&writeBuffer[bufferIndex], node->message, messageLength);
            bufferIndex += messageLength;
            writeBuffer[bufferIndex++] = '\n';
            free(node);
        }
    }

    if (bufferIndex > 0) {
        fwrite(writeBuffer, 1, bufferIndex, logFile);
    }

    return 0;
}

void GetElapsedTime(FILETIME startTime, double* elapsedSeconds) {
    FILETIME currentTime;
    ULARGE_INTEGER start, end;
    GetSystemTimeAsFileTime(&currentTime);
    start.LowPart = startTime.dwLowDateTime;
    start.HighPart = startTime.dwHighDateTime;
    end.LowPart = currentTime.dwLowDateTime;
    end.HighPart = currentTime.dwHighDateTime;
    *elapsedSeconds = (double)(end.QuadPart - start.QuadPart) / 10000000.0;
}
