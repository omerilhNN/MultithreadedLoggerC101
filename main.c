#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>
#include <string.h>

#define NUM_THREADS 10
#define MAX_LOG_ENTRIES 1000000
#define LOG_FILE_PATH "logs\\log.txt"
#define BATCH_SIZE 1000
#define LOG_MESSAGE_SIZE 256
#define BUFFERED_WRITE_SIZE (BATCH_SIZE * LOG_MESSAGE_SIZE)

typedef struct LogNode {
    char message[LOG_MESSAGE_SIZE];
    struct LogNode* next;
} LogNode;

typedef struct {
    LogNode* head;
    LogNode* tail;
    CRITICAL_SECTION lock;  // For thread-safe access
    CONDITION_VARIABLE notEmpty; // Condition variable for signaling
} ConcurrentQueue;


void enqueue(ConcurrentQueue* queue, const char* message);
LogNode* dequeue(ConcurrentQueue* queue);
void initialize_queue(ConcurrentQueue* queue);
void produce_log_message(ConcurrentQueue* queue, int threadNumber, FILETIME startTime);
DWORD WINAPI log_producer(LPVOID arg);
DWORD WINAPI log_writer(LPVOID arg);
void GetElapsedTime(FILETIME startTime, double* elapsedSeconds);

// Global variables
ConcurrentQueue logQueue;
HANDLE loggingThreads[NUM_THREADS];
HANDLE logWriterThread;
volatile LONG isRunning = 1;
volatile LONG logCounter = 0;
FILETIME startTime;

int main() {
    FILETIME endTime;
    double elapsedSeconds;

    // Retrieve the current system date and time. As UTC.
    GetSystemTimeAsFileTime(&startTime);

    // Initialize the concurrent queue
    initialize_queue(&logQueue);

    logWriterThread = CreateThread(NULL, 0, log_writer, NULL, 0, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        int* threadNumber = (int*)malloc(sizeof(int));
        *threadNumber = i;
        loggingThreads[i] = CreateThread(NULL, 0, log_producer, threadNumber, 0, NULL);
    }

    WaitForMultipleObjects(NUM_THREADS, loggingThreads, TRUE, INFINITE);

    InterlockedExchange(&isRunning, 0);
    WaitForSingleObject(logWriterThread, INFINITE);

    DeleteCriticalSection(&logQueue.lock);

    GetSystemTimeAsFileTime(&endTime);
    GetElapsedTime(startTime, &elapsedSeconds);

    printf("Total execution time: %.4f seconds\n", elapsedSeconds);

    return 0;
}

void produce_log_message(ConcurrentQueue* queue, int threadNumber, FILETIME startTime) {
    char logMessage[LOG_MESSAGE_SIZE];
    double elapsedSeconds;

    while (InterlockedIncrement(&logCounter) <= MAX_LOG_ENTRIES) {
        GetElapsedTime(startTime, &elapsedSeconds);
        snprintf(logMessage, sizeof(logMessage), "%.6f | Thread %d | Logging message %ld", elapsedSeconds, threadNumber, logCounter);
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
    FILE* logFile = fopen(LOG_FILE_PATH, "a");
    if (logFile == NULL) {
        perror("Failed to open log file");
        return 1;
    }

    char writeBuffer[BUFFERED_WRITE_SIZE];
    size_t bufferIndex = 0;

    while (InterlockedCompareExchange(&isRunning, 1, 1) || logQueue.head != NULL) {
        LogNode* node = dequeue(&logQueue);
        if (node) {
            size_t messageLength = strlen(node->message);
            if (bufferIndex + messageLength + 1 > BUFFERED_WRITE_SIZE) {
                // Flush the buffer if it's full
                fwrite(writeBuffer, 1, bufferIndex, logFile);
                bufferIndex = 0;
            }
            memcpy(&writeBuffer[bufferIndex], node->message, messageLength);
            bufferIndex += messageLength;
            writeBuffer[bufferIndex++] = '\n'; // Add newline after each message
            free(node);
        }
    }

    // Flush remaining messages in the buffer
    if (bufferIndex > 0) {
        fwrite(writeBuffer, 1, bufferIndex, logFile);
    }

    fclose(logFile);
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

void initialize_queue(ConcurrentQueue* queue) {
    queue->head = NULL;
    queue->tail = NULL;
    InitializeCriticalSection(&queue->lock);
    InitializeConditionVariable(&queue->notEmpty);
}

// Enqueue operation (thread-safe)
void enqueue(ConcurrentQueue* queue, const char* message) {
    LogNode* newNode = (LogNode*)malloc(sizeof(LogNode));
    if (newNode == NULL) {
        perror("Failed to allocate memory for log node");
        exit(EXIT_FAILURE);
    }
    //_strdup yerine strcpy kullanýldý -> performance optimization
    strcpy_s(newNode->message, sizeof(newNode->message), message);
    newNode->next = NULL;
    
    //Queue'ya ayný anda birden fazla eriþim olduðundan dolayý locklama iþlemini gerçekleþtir
    EnterCriticalSection(&queue->lock);
    if (queue->tail) {
        queue->tail->next = newNode;
    }
    else {
        queue->head = newNode;
    }
    queue->tail = newNode;
    LeaveCriticalSection(&queue->lock);

    // Signal that the queue is not empty // Bu condition variable'da bekleyen threadi uyandýr
    WakeConditionVariable(&queue->notEmpty);
}

// Dequeue operation (thread-safe)
LogNode* dequeue(ConcurrentQueue* queue) {
    EnterCriticalSection(&queue->lock);
    while (queue->head == NULL) {
        SleepConditionVariableCS(&queue->notEmpty, &queue->lock, INFINITE);
    }

    LogNode* node = queue->head;
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    LeaveCriticalSection(&queue->lock);

    return node;
}
