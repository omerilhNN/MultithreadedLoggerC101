#include "logger.h"

int main() {
    LoggerContext logger;

    initialize_logger(&logger, MAX_LOG_ENTRIES);

    start_logging(&logger);

    stop_logging(&logger);

    return 0;
}
