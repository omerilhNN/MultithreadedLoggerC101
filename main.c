#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>
#include <string.h>

#define NUM_THREADS 10
#define NUM_LOG_ENTRIES 100000
#define BATCH_SIZE 1000
#define LOG_FILE_PATH "logs\\log.txt"

typedef struct LogNode {
    char* message;
    struct LogNode* next;
} LogNode;

typedef struct {
    LogNode* head;
    LogNode* tail;
    LONG size;
    CRITICAL_SECTION lock;
} AtomicQueue;

void enqueue(AtomicQueue* queue, char* message) {
    LogNode* newNode = (LogNode*)malloc(sizeof(LogNode));
    newNode->message = message;
    newNode->next = NULL;

    EnterCriticalSection(&queue->lock);
    LogNode* prevTail = queue->tail;
    if (prevTail != NULL) {
        prevTail->next = newNode;
    }
    else {
        queue->head = newNode;
    }
    queue->tail = newNode;
    InterlockedIncrement(&queue->size);
    LeaveCriticalSection(&queue->lock);
}

char* dequeue(AtomicQueue* queue) {
    EnterCriticalSection(&queue->lock);
    LogNode* head = queue->head;
    if (head == NULL) {
        LeaveCriticalSection(&queue->lock);
        return NULL;
    }
    queue->head = head->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    char* message = head->message;
    free(head);
    InterlockedDecrement(&queue->size);
    LeaveCriticalSection(&queue->lock);
    return message;
}

int is_queue_empty(AtomicQueue* queue) {
    return InterlockedCompareExchange(&queue->size, 0, 0) == 0;
}
AtomicQueue logQueue = { NULL, NULL, 0 };
HANDLE loggingThreads[NUM_THREADS];
HANDLE logWriterThread;
volatile LONG isRunning = 1;
CRITICAL_SECTION fileMutex;

DWORD WINAPI log_producer(LPVOID arg) {
    int threadNumber = *(int*)arg;
    free(arg);

    for (int i = 0; i < NUM_LOG_ENTRIES; i++) {
        char* logMessage = (char*)malloc(256);
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        strftime(logMessage, 256, "%Y-%m-%dT%H:%M:%S", &timeinfo);
        snprintf(logMessage + strlen(logMessage), 256 - strlen(logMessage), ".%03d Thread %d: %s", (int)(now % 1000), threadNumber, logMessage);

        enqueue(&logQueue, logMessage);
    }

    return 0;
}

DWORD WINAPI log_writer(LPVOID arg) {
    FILE* logFile = fopen(LOG_FILE_PATH, "a");
    if (logFile == NULL) {
        perror("Failed to open log file");
        return 1;
    }

    char* batch[BATCH_SIZE];
    int batchCount = 0;

    while (InterlockedCompareExchange(&isRunning, 1, 1) || !is_queue_empty(&logQueue)) {
        while (batchCount < BATCH_SIZE && (batch[batchCount] = dequeue(&logQueue)) != NULL) {
            batchCount++;
        }

        if (batchCount > 0) {
            EnterCriticalSection(&fileMutex);
            for (int i = 0; i < batchCount; i++) {
                fprintf(logFile, "%s\n", batch[i]);
                free(batch[i]);
            }
            LeaveCriticalSection(&fileMutex);
            batchCount = 0;
        }
        else {
            Sleep(10); 
        }
    }

    while ((batch[batchCount] = dequeue(&logQueue)) != NULL) {
        EnterCriticalSection(&fileMutex);
        fprintf(logFile, "%s\n", batch[batchCount]);
        free(batch[batchCount]);
        LeaveCriticalSection(&fileMutex);
    }

    fclose(logFile);
    return 0;
}

int main() {
    CreateDirectory("logs", NULL);

    InitializeCriticalSection(&fileMutex);
    InitializeCriticalSection(&logQueue.lock);

    logWriterThread = CreateThread(NULL, 0, log_writer, NULL, 0, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        int* threadNumber = (int*)malloc(sizeof(int));
        *threadNumber = i;
        loggingThreads[i] = CreateThread(NULL, 0, log_producer, threadNumber, 0, NULL);
    }

    WaitForMultipleObjects(NUM_THREADS, loggingThreads, TRUE, INFINITE);

    InterlockedExchange(&isRunning, 0);
    WaitForSingleObject(logWriterThread, INFINITE);

    DeleteCriticalSection(&fileMutex);
    DeleteCriticalSection(&logQueue.lock);

    return 0;
}
