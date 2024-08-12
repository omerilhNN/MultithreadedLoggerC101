#include "logger.h"

int main() {
    FILETIME endTime;
    double elapsedSeconds;

    GetSystemTimeAsFileTime(&startTime);

    FILE* logFile = fopen(LOG_FILE_PATH, "a");
    if (logFile == NULL) {
        perror("Failed to open log file");
        return 1;
    }

    initialize_queue(&logQueue, MAX_LOG_ENTRIES);

    logWriterThread = CreateThread(NULL, 0, log_writer, logFile, 0, NULL);

    HANDLE loggingThreads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int* threadNumber = (int*)malloc(sizeof(int));
        if (threadNumber == NULL) {
            perror("Failed to allocate memory for thread number");
            return 1;
        }
        *threadNumber = i;
        loggingThreads[i] = CreateThread(NULL, 0, log_producer, threadNumber, 0, NULL);
        if (loggingThreads[i] == NULL) {
            perror("Failed to create logging thread");
            return 1;
        }
    }

    WaitForMultipleObjects(NUM_THREADS, loggingThreads, TRUE, INFINITE);

    EnterCriticalSection(&logQueue.cs);
    isRunning = 0;
    WakeConditionVariable(&logQueue.notEmpty);  // Wake up writer if waiting
    LeaveCriticalSection(&logQueue.cs);

    WaitForSingleObject(logWriterThread, INFINITE);

    fclose(logFile);

    destroy_queue(&logQueue);

    GetSystemTimeAsFileTime(&endTime);
    GetElapsedTime(startTime, &elapsedSeconds);

    printf("Total execution time: %.4f seconds\n", elapsedSeconds);

    return 0;
}
