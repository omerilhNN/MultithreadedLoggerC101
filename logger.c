#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global logger context
static LoggerContext logger;

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
        EnterCriticalSection(&queue->cs);
        if (logger.logCounter >= MAX_LOG_ENTRIES) {
            LeaveCriticalSection(&queue->cs);
            break;
        }
        logger.logCounter++;
        LeaveCriticalSection(&queue->cs);

        GetElapsedTime(startTime, &elapsedSeconds);
        snprintf(logMessage, sizeof(logMessage), "%.6f | Thread %d | Logging message %d", elapsedSeconds, threadNumber, logger.logCounter);
        logMessage[sizeof(logMessage) - 1] = '\0';

        enqueue(queue, logMessage);
    }
}

DWORD WINAPI log_producer(LPVOID arg) {
    int threadNumber = *(int*)arg;
    free(arg);
    produce_log_message(&logger.logQueue, threadNumber, logger.startTime);
    return 0;
}

DWORD WINAPI log_writer(LPVOID arg) {
    FILE* logFile = (FILE*)arg;

    char writeBuffer[BUFFERED_WRITE_SIZE];
    size_t bufferIndex = 0;

    while (1) {
        EnterCriticalSection(&logger.logQueue.cs);
        while (logger.logQueue.count == 0 && logger.isRunning) {
            SleepConditionVariableCS(&logger.logQueue.notEmpty, &logger.logQueue.cs, INFINITE);
        }
        if (!logger.isRunning && logger.logQueue.count == 0) {
            LeaveCriticalSection(&logger.logQueue.cs);
            break;
        }
        LogNode* node = dequeue(&logger.logQueue);
        LeaveCriticalSection(&logger.logQueue.cs);

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

void initialize_logger(int capacity) {
    logger.isRunning = 1;
    logger.logCounter = 0;
    GetSystemTimeAsFileTime(&logger.startTime);
    initialize_queue(&logger.logQueue, capacity);
}

void start_logging() {
    FILE* logFile = fopen(LOG_FILE_PATH, "a");
    if (logFile == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    logger.logWriterThread = CreateThread(NULL, 0, log_writer, logFile, 0, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        int* threadNumber = (int*)malloc(sizeof(int));
        if (threadNumber == NULL) {
            perror("Failed to allocate memory for thread number");
            exit(EXIT_FAILURE);
        }
        *threadNumber = i;
        logger.loggingThreads[i] = CreateThread(NULL, 0, log_producer, threadNumber, 0, NULL);
        if (logger.loggingThreads[i] == NULL) {
            perror("Failed to create logging thread");
            exit(EXIT_FAILURE);
        }
    }
}

void stop_logging() {
    WaitForMultipleObjects(NUM_THREADS, logger.loggingThreads, TRUE, INFINITE);

    EnterCriticalSection(&logger.logQueue.cs);
    logger.isRunning = 0;
    WakeConditionVariable(&logger.logQueue.notEmpty);
    LeaveCriticalSection(&logger.logQueue.cs);

    WaitForSingleObject(logger.logWriterThread, INFINITE);
    fclose(logger.logWriterThread);

    destroy_queue(&logger.logQueue);
}
