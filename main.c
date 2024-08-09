#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>
#include <string.h>

#define NUM_THREADS 10
#define MAX_LOG_ENTRIES 1000000
#define BUFFER_SIZE 8192 // Must be a power of 2
#define LOG_FILE_PATH "logs\\log.txt"
#define BATCH_SIZE 1000

typedef struct {
    char* buffer[BUFFER_SIZE];
    volatile LONG write_pos;
    volatile LONG read_pos;
} RingBuffer;

void produce_log_message(RingBuffer* ring_buffer, int threadNumber, FILETIME startTime);
DWORD WINAPI log_producer(LPVOID arg);
DWORD WINAPI log_writer(LPVOID arg);

RingBuffer ring_buffer;
HANDLE loggingThreads[NUM_THREADS];
HANDLE logWriterThread;
volatile LONG isRunning = 1;
volatile LONG logCounter = 0;
HANDLE fileMutex;
FILETIME startTime;

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

int main() {
    FILETIME endTime;
    double elapsedSeconds;

    //Retrieves the current system date and time. As UTC.
    GetSystemTimeAsFileTime(&startTime);

    memset(&ring_buffer, 0, sizeof(RingBuffer));
    
    if (CreateDirectory("logs", NULL) != 0) {
        printf("Error Creating directory: %d\n",GetLastError());
    }
    
    fileMutex = CreateMutex(NULL, FALSE, NULL); //initial ownership is false

    logWriterThread = CreateThread(NULL, 0, log_writer, NULL, 0, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        int* threadNumber = (int*)malloc(sizeof(int));
        *threadNumber = i;
        loggingThreads[i] = CreateThread(NULL, 0, log_producer, threadNumber, 0, NULL);
    }

    WaitForMultipleObjects(NUM_THREADS, loggingThreads, TRUE, INFINITE);

    //Atomic olarak bu iki deðerin deðiþimini saðlar (Ayný anda birden fazla threadin eriþimini engeller)
    InterlockedExchange(&isRunning, 0);
    WaitForSingleObject(logWriterThread, INFINITE);

    CloseHandle(fileMutex);

    GetSystemTimeAsFileTime(&endTime);

    GetElapsedTime(startTime, &elapsedSeconds);

    printf("Total execution time: %.4f seconds\n", elapsedSeconds);

    return 0;
}

void produce_log_message(RingBuffer* ring_buffer, int threadNumber, FILETIME startTime) {

    char logMessage[256];
    while (InterlockedIncrement(&logCounter) <= MAX_LOG_ENTRIES) {
        double elapsedSeconds;
        GetElapsedTime(startTime, &elapsedSeconds);

        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);

        snprintf(logMessage, sizeof(logMessage), "%.6f | Thread %d | Logging message %d", elapsedSeconds, threadNumber, logCounter);

        logMessage[sizeof(logMessage) - 1] = '\0';

        LONG write_pos;
        while (1) {
            write_pos = ring_buffer->write_pos;
            LONG next_write_pos = (write_pos + 1) & (BUFFER_SIZE - 1);
            if (next_write_pos == ring_buffer->read_pos) {
                // Buffer is full, wait until space is available
                Sleep(1);
            }
            else {
                if (InterlockedCompareExchange(&ring_buffer->write_pos, next_write_pos, write_pos) == write_pos) {
                    break;
                }
            }
        }

        // Allocate memory for the log message
        char* logMessageCopy = _strdup(logMessage);
        if (logMessageCopy == NULL) {
            perror("Failed to allocate memory for log message");
            exit(EXIT_FAILURE);
        }
        //Eðer  = logMessage yaparsak -> logMessage local bir deðiþken olduðundan ve ring_buffer'ýn bufferýný onun adresine eþitlediðimizden dolayý function scope'undan çýkýldýðýnda 
        //geçersiz bir deðer olur o yüzden _strdup kullanýlýr ve memory allocation saðlanýr ve o addresteki deðere eþitlemesi yapýlýr.
        ring_buffer->buffer[write_pos] = logMessageCopy;
    }
}

DWORD WINAPI log_producer(LPVOID arg) {
    int threadNumber = *(int*)arg;
    free(arg);
    produce_log_message(&ring_buffer, threadNumber, startTime);
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

    //InterlockedCompareExchange: isRunning deðerini 3.Parametre (Comperand) ile karþýlaþtýr deðerler eþit ise 2.parametredeki deðeri isRunning'e atamasýný yap.
    while (InterlockedCompareExchange(&isRunning, 1, 1) || (ring_buffer.read_pos != ring_buffer.write_pos)) {
        while (batchCount < BATCH_SIZE && (ring_buffer.read_pos != ring_buffer.write_pos)) {
            LONG read_pos = ring_buffer.read_pos;
            if (InterlockedCompareExchange(&ring_buffer.read_pos, (read_pos + 1) & (BUFFER_SIZE - 1), read_pos) == read_pos) {
                batch[batchCount++] = ring_buffer.buffer[read_pos];
            }
        }

        if (batchCount > 0) {
            WaitForSingleObject(fileMutex, INFINITE);
            for (int i = 0; i < batchCount; i++) {
                fprintf(logFile, "%s\n", batch[i]);
                free(batch[i]);
            }
            ReleaseMutex(fileMutex);
            batchCount = 0;
        }
        else {
            Sleep(10); // Sleep for 10ms to reduce CPU usage
        }
    }

    // Process any remaining messages
    while (ring_buffer.read_pos != ring_buffer.write_pos) {
        LONG read_pos = ring_buffer.read_pos;
        if (InterlockedCompareExchange(&ring_buffer.read_pos, (read_pos + 1) & (BUFFER_SIZE - 1), read_pos) == read_pos) {
            char* logMessage = ring_buffer.buffer[read_pos];
            WaitForSingleObject(fileMutex, INFINITE);
            fprintf(logFile, "%s\n", logMessage);
            ReleaseMutex(fileMutex);
            free(logMessage);
        }
    }

    fclose(logFile);
    return 0;
}
