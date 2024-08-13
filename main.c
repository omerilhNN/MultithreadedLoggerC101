#include "logger.h"

int main() {
    double elapsedSeconds;

    initialize_logger(MAX_LOG_ENTRIES);

    start_logging();

    stop_logging();

    return 0;
}
