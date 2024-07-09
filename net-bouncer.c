// cc -std=c99 -Wall -Wextra -pedantic -Wno-missing-field-initializers net-that.c -o net-that

#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>

enum log_level
{
    LOG_ERROR = 0,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
};

static const char *LOG_LEVELS[] =
{
    "ERROR",
    "WARNING",
    "INFO",
    "DEBUG"
};

static bool global_running = true;
static const char *global_log_file = NULL;
static FILE *global_log = NULL;
static enum log_level global_level = LOG_INFO;
static int global_family = AF_INET;
static int global_port = 0;
static const int global_max_connections = 50;

int64_t current_time_ms()
{
    struct timespec tv;
    clock_gettime(CLOCK_REALTIME, &tv);
    return tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

static void log_message(enum log_level level, const char *format, ...)
{
    if (level > global_level || level < 0)
        return;

    // time
    int64_t now = current_time_ms();
    time_t t = now / 1000;
    char date[64];
    struct tm tm;
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime_r(&t, &tm));
    fprintf(global_log, "%s.%03lu [%s] ", date, now % 1000, LOG_LEVELS[level]);

    // message
    va_list args;
    va_start(args, format);
    vfprintf(global_log, format, args);
    va_end(args);
    fputc('\n', global_log);
}

static void log_connection( enum log_level level, struct sockaddr *source )
{
    char address[INET_ADDRSTRLEN];
    if (source->sa_family == AF_INET)
        inet_ntop(AF_INET, &((struct sockaddr_in*)source)->sin_addr, address, sizeof(struct sockaddr_in));
    else
        inet_ntop(AF_INET6, &((struct sockaddr_in6*)source)->sin6_addr, address, sizeof(struct sockaddr_in6));
    log_message(level, "Connection from %s", address);
}

static void log_error(int err)
{
    log_message(LOG_ERROR, "%s", strerror(err));
}

static void signal_handler(int signum)
{
    log_message(LOG_WARNING, "Caught signal %d!", signum);
    global_running = false;
}

static void parse_help(char * const *argv)
{
    fprintf(stderr, "Usage: %s -p port [ -l log_file ]\n", argv[0]);
}

static bool parse_options(int argc, char * const *argv)
{
    int option = 0;
    while ((option = getopt(argc, argv, "p:l:")) >= 0)
    {
        switch (option)
        {
            case 'p':
                global_port = atoi(optarg);
                break;
            case 'l':
                global_log_file = optarg;
                break;
            default:
                parse_help(argv);
                return false;
        }
    }

    if (global_port == 0)
    {
        fprintf(stderr, "%s: missing port number\n", argv[0]);
        parse_help(argv);
        return false;
    }

    return true;
}

static int create_server(int port, int family, int max_connections)
{
    if (port <= 0 || port > 65535 || (family != AF_INET && family != AF_INET6))
        return -EINVAL;
    if (max_connections <= 0)
        max_connections = 5;

    int conn = socket(family == AF_UNSPEC ? AF_INET : family, SOCK_STREAM, 0);
    if (conn < 0)
        return conn;

    int value = 1;
    if (setsockopt(conn, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0)
        log_message(LOG_WARNING, "Unable to make the address reusable; %s", strerror(errno));

    int result = 0;
    if (global_family == AF_INET)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons( (uint16_t) port);
        addr.sin_addr.s_addr = INADDR_ANY;
        memset(&addr.sin_zero, 0, sizeof(addr.sin_zero));
        result = bind(conn, (const struct sockaddr *) &addr, sizeof(addr));
    }
    else
    {
        struct sockaddr_in6 addr;
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons( (uint16_t) port);
        addr.sin6_addr = in6addr_any;
        result = bind(conn, (const struct sockaddr *) &addr, sizeof(addr));
    }
    if (result < 0)
    {
        close(conn);
        return result;
    }

    result = listen(conn, max_connections);
    if (result < 0)
    {
        close(conn);
        return result;
    }

    return conn;
}

int main(int argc, char** argv)
{
    if (!parse_options(argc, argv))
        return 1;

    if (global_log_file)
    {
        global_log = fopen(global_log_file, "at");
        if (global_log == NULL)
        {
            global_log = stderr;
            int err = errno;
            log_message(LOG_ERROR, "Unable to open log file '%s'", global_log_file);
            log_error(err);
            return 1;
        }
    }
    else
        global_log = stderr;

    int conn = create_server(global_port, global_family, global_max_connections);
    if (conn < 0)
    {
        log_error(conn);
        return 1;
    }
    log_message(LOG_INFO, "Listening to any address at port %d", global_port);

    // capture signals to terminate the program
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    while (global_running)
    {
        struct sockaddr_in6 address;
        socklen_t len = sizeof(address);
        int client = accept(conn, (struct sockaddr *) &address, &len);
        if (client < 0)
        {
            perror("Accept error");
            break;
        }

        log_connection(LOG_INFO, (struct sockaddr *) &address);
        close(client);
    }

    close(conn);
    return 0;
}
