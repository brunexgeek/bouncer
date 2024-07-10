/*
 * net-bouncer
 * Honeypot program that logs connection attempts and refuses them
 *
 * Copyright 2024 Bruno Costa
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

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
#include <poll.h>

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

static const int VERSION_MAJOR = 0;
static const int VERSION_MINOR = 1;
static const int VERSION_PATCH = 0;

#define MAX_CONNECTIONS  50
#define MAX_PORTS        50

static bool global_running = true;
static const char *global_log_file = NULL;
static FILE *global_log = NULL;
static enum log_level global_level = LOG_INFO;
static int global_family = AF_INET;
static int global_ports[MAX_PORTS];
static int global_port_count = 0;

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
    fflush(global_log);
}

static void log_connection_ipv4( enum log_level level, const struct sockaddr_in *source )
{
    char address[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &source->sin_addr, address, sizeof(struct sockaddr_in));
    log_message(level, "Connection from %s on port %d", address, source->sin_port);
}

static void log_connection_ipv6( enum log_level level, const struct sockaddr_in6 *source )
{
    char address[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &source->sin6_addr, address, sizeof(struct sockaddr_in6));
    log_message(level, "Connection from %s on port %d", address, source->sin6_port);
}

static void log_error(const char *message, int err)
{
    log_message(LOG_ERROR, "%s: %s", message, strerror(err));
}

static void signal_handler(int signum)
{
    log_message(LOG_WARNING, "Caught signal %d!", signum);
    global_running = false;
}

static void parse_help(char * const *argv)
{
    fprintf(stderr, "Usage: %s -p port1 [ -p port2 ... ] [ -l log_file ] [ -4 | -6 ]\n\n", argv[0]);
    fputs("-p number     Listen on the specified port; this option may appear multiple times.\n"
        "-l log_file   Path to the log file; if omitted, the log will be output to 'stderr'.\n"
        "-4            Listen for IPv4 connections (any address); this is the default.\n"
        "-6            Listen for IPv6 connections (any address).\n",
        stderr);
}

static bool parse_options(int argc, char * const *argv)
{
    int option = 0;
    while ((option = getopt(argc, argv, "p:l:46")) >= 0)
    {
        switch (option)
        {
            case 'p':
                if (global_port_count >= MAX_PORTS)
                {
                    fprintf(stderr, "%s: too many ports; you must specify at most %d ports\n", argv[0], MAX_PORTS);
                    return false;
                }
                global_ports[global_port_count++] = atoi(optarg);
                break;
            case 'l':
                global_log_file = optarg;
                break;
            case '4':
                global_family = AF_INET;
                break;
            case '6':
                global_family = AF_INET6;
                break;
            default:
                parse_help(argv);
                return false;
        }
    }

    if (global_port_count == 0)
    {
        fprintf(stderr, "%s: missing port number\n", argv[0]);
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
    if (global_family == AF_INET6)
    {
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons( (uint16_t) port);
        addr.sin6_addr = in6addr_any;
        result = bind(conn, (const struct sockaddr *) &addr, sizeof(addr));
    }
    else
    {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons( (uint16_t) port);
        addr.sin_addr.s_addr = INADDR_ANY;
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
    {
        parse_help(argv);
        return 1;
    }

    if (global_log_file)
    {
        global_log = fopen(global_log_file, "at");
        if (global_log == NULL)
        {
            global_log = stderr;
            int err = errno;
            log_message(LOG_ERROR, "Unable to open log file '%s'", global_log_file);
            log_error("IO error", err);
            return 1;
        }
    }
    else
        global_log = stderr;

    fprintf(global_log, "\nnet-bouncer %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    // create the server socket
    struct pollfd wait_list[MAX_PORTS];
    memset(wait_list, 0, sizeof(wait_list));
    for (int p = 0; global_port_count > p; ++p)
    {
        wait_list[p].events = POLLIN;
        wait_list[p].fd = create_server(global_ports[p], global_family, MAX_CONNECTIONS);
        if (wait_list[p].fd < 0)
        {
            log_error("Unable to create socket server", wait_list[p].fd);
            return 1;
        }
        log_message(LOG_INFO, "Listening to any address on the port %d", global_ports[p]);
    }

    // capture signals to terminate the program
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    // keep accepting clients until the program finishes
    while (global_running)
    {
        int events = poll(wait_list, (nfds_t) global_port_count, -1);
        if (events < 0)
        {
            log_error("Error waiting connection", errno);
            break;
        }

        for (int p = 0; p <= global_port_count && events > 0; ++p)
        {
            if ((wait_list[p].revents & POLLIN) == 0)
                continue;
            --events;
            wait_list[p].revents = 0;

            struct sockaddr_in6 address;
            socklen_t len = sizeof(address);
            int client = accept(wait_list[p].fd, (struct sockaddr *) &address, &len);
            if (client < 0)
            {
                log_error("Error accepting connection", errno);
                break;
            }
            // log and close the connection
            if (global_family == AF_INET6)
                log_connection_ipv6(LOG_INFO, (const struct sockaddr_in6 *) &address);
            else
                log_connection_ipv4(LOG_INFO, (const struct sockaddr_in *) &address);
            close(client);
        }
    }

    for (int p = 0; p < global_port_count; ++p)
        close(wait_list[p].fd);
    return 0;
}
