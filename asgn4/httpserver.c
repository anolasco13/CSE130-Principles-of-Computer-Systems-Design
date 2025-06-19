/**
 * httpserver.c
 *
 * A multi-threaded HTTP/1.1 server that implements GET and PUT requests.
 * It uses helper functions for robust I/O, a listener socket abstraction,
 * a thread-safe queue (from asgn3), and reader/writer locks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <regex.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "connection.h"
#include "response.h"
#include "request.h"
#include "listener_socket.h"   // ls_new, ls_accept, ls_delete
#include "iowrapper.h"         // write_n_bytes, read_n_bytes, pass_n_bytes
#include "queue.h"             // thread-safe queue for requests
#include "rwlock.h"            // reader/writer lock for file access

// Maximum sizes for the request
#define MAX_REQUEST_SIZE 8192
#define MAX_HEADERS 128

// Our request-line must match: METHOD SP URI SP HTTP/x.y
#define REQUEST_LINE_PATTERN "^([a-zA-Z]{1,8}) (/[a-zA-Z0-9.-]{1,63}) (HTTP/[0-9]\\.[0-9])$"
#define CONTENT_LENGTH_REGEX "Content-Length: ([[:digit:]]+)"

// Response definitions.
#define RESPONSE_400 "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n"
#define RESPONSE_403 "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n"
#define RESPONSE_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n"
#define RESPONSE_500 "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n"
#define RESPONSE_501 "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n"
#define RESPONSE_505 "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not Supported\n"

// For successful PUT responses:
#define PUT_RESPONSE_OK "OK\n"         // length 3
#define PUT_RESPONSE_CREATED "Created\n" // length 8

// Global audit log mutex to ensure atomic writes.
pthread_mutex_t audit_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* --- New per-file lock table --- */
typedef struct {
    int client_fd;
} request_item_t;

queue_t *request_queue;

typedef struct file_lock_entry {
    char *filepath;
    rwlock_t *lock;
    struct file_lock_entry *next;
} file_lock_entry_t;

static file_lock_entry_t *lock_table = NULL;
static pthread_mutex_t lock_table_mutex = PTHREAD_MUTEX_INITIALIZER;

static rwlock_t *get_file_lock(const char *filepath) {
    pthread_mutex_lock(&lock_table_mutex);
    file_lock_entry_t *entry = lock_table;
    while (entry != NULL) {
        if (strcmp(entry->filepath, filepath) == 0) {
            pthread_mutex_unlock(&lock_table_mutex);
            return entry->lock;
        }
        entry = entry->next;
    }
    // Not found, create new entry.
    entry = malloc(sizeof(file_lock_entry_t));
    if (entry == NULL) {
        pthread_mutex_unlock(&lock_table_mutex);
        return NULL;
    }
    entry->filepath = strdup(filepath);
    if (entry->filepath == NULL) {
        free(entry);
        pthread_mutex_unlock(&lock_table_mutex);
        return NULL;
    }
    entry->lock = rwlock_new(WRITERS, 0);
    entry->next = lock_table;
    lock_table = entry;
    pthread_mutex_unlock(&lock_table_mutex);
    return entry->lock;
}

/* Read HTTP request until "\r\n\r\n" or buffer full. */
static ssize_t read_http_request(int fd, char *buf, size_t maxlen) {
    size_t total = 0;
    while (total < maxlen - 1) {
        ssize_t n = read(fd, buf + total, 1);
        if (n < 0) {
            return -1; // error
        }
        if (n == 0) {
            break; // connection closed
        }
        total += n;
        buf[total] = '\0';
        if (total >= 4 && strstr(buf, "\r\n\r\n") != NULL) {
            break;
        }
    }
    return total;
}

/* Send entire message string. */
static int send_response(int fd, const char *msg) {
    size_t len = strlen(msg);
    if (write_n_bytes(fd, (char *)msg, len) != (ssize_t)len) {
        return -1;
    }
    return 0;
}

/* Write an audit log entry to stderr.
 * Format: <Oper>,<URI>,<Status-Code>,<RequestID header value>\n
 * If RequestID header not found, use "0". */
static void write_audit_log(const char *oper, const char *uri, int status_code,
                            const char *req_id) {
    pthread_mutex_lock(&audit_log_mutex);
    fprintf(stderr, "%s,%s,%d,%s\n", oper, uri, status_code, (req_id ? req_id : "0"));
    fflush(stderr);
    pthread_mutex_unlock(&audit_log_mutex);
}

/* Process one connection (client_fd) using the same logic as assignment 2.
 * Now we add per-file synchronization and audit logging.
 * For GET, we acquire the file’s reader lock; for PUT, its writer lock.
 */
static void process_connection(int client_fd) {
    char request[MAX_REQUEST_SIZE];
    memset(request, 0, sizeof(request));
    ssize_t req_len = read_http_request(client_fd, request, sizeof(request));
    if (req_len <= 0) {
        close(client_fd);
        return;
    }
    if (strstr(request, "\r\n\r\n") == NULL) {
        send_response(client_fd, RESPONSE_400);
        close(client_fd);
        return;
    }

    /* Make a copy for PUT usage. */
    char orig_request[MAX_REQUEST_SIZE];
    strncpy(orig_request, request, req_len);
    orig_request[req_len] = '\0';

    /* Tokenize request into lines. */
    char *saveptr;
    char *line = strtok_r(request, "\r\n", &saveptr);
    if (line == NULL) {
        send_response(client_fd, RESPONSE_400);
        close(client_fd);
        return;
    }

    /* Validate the request-line. */
    regex_t req_regex;
    if (regcomp(&req_regex, REQUEST_LINE_PATTERN, REG_EXTENDED) != 0) {
        send_response(client_fd, RESPONSE_500);
        close(client_fd);
        return;
    }
    regmatch_t matches[4];
    if (regexec(&req_regex, line, 4, matches, 0) == REG_NOMATCH) {
        regfree(&req_regex);
        send_response(client_fd, RESPONSE_400);
        close(client_fd);
        return;
    }
    int method_len = matches[1].rm_eo - matches[1].rm_so;
    int uri_len = matches[2].rm_eo - matches[2].rm_so;
    int version_len = matches[3].rm_eo - matches[3].rm_so;
    char method[16], uri[128], version[16];
    if (method_len >= (int)sizeof(method) ||
        uri_len >= (int)sizeof(uri) ||
        version_len >= (int)sizeof(version)) {
        regfree(&req_regex);
        send_response(client_fd, RESPONSE_400);
        close(client_fd);
        return;
    }
    strncpy(method, line + matches[1].rm_so, method_len);
    method[method_len] = '\0';
    strncpy(uri, line + matches[2].rm_so, uri_len);
    uri[uri_len] = '\0';
    strncpy(version, line + matches[3].rm_so, version_len);
    version[version_len] = '\0';
    regfree(&req_regex);

    /* Check HTTP version. */
    if (strcmp(version, "HTTP/1.1") != 0) {
        regex_t ver_regex;
        if (regcomp(&ver_regex, "^HTTP/[0-9]\\.[0-9]$", REG_EXTENDED) != 0) {
            send_response(client_fd, RESPONSE_500);
            close(client_fd);
            return;
        }
        if (regexec(&ver_regex, version, 0, NULL, 0) == 0) {
            regfree(&ver_regex);
            send_response(client_fd, RESPONSE_505);
        } else {
            regfree(&ver_regex);
            send_response(client_fd, RESPONSE_400);
        }
        close(client_fd);
        return;
    }

    /* Check method: must be GET or PUT. */
    if (strcmp(method, "GET") != 0 && strcmp(method, "PUT") != 0) {
        regex_t m_regex;
        if (regcomp(&m_regex, "^[a-zA-Z]{1,8}$", REG_EXTENDED) != 0) {
            send_response(client_fd, RESPONSE_500);
            close(client_fd);
            return;
        }
        if (regexec(&m_regex, method, 0, NULL, 0) == 0) {
            regfree(&m_regex);
            send_response(client_fd, RESPONSE_501);
        } else {
            regfree(&m_regex);
            send_response(client_fd, RESPONSE_400);
        }
        close(client_fd);
        return;
    }

    /* Validate header lines. */
    char *header_line;
    char *req_id = NULL;
    while ((header_line = strtok_r(NULL, "\r\n", &saveptr)) != NULL &&
           header_line[0] != '\0') {
        /* If header is "Request-Id", save its value. */
        if (strncasecmp(header_line, "Request-Id:", 11) == 0) {
            char *val = header_line + 11;
            while (*val == ' ')
                val++;
            req_id = strdup(val);
        }
        regex_t header_regex;
        const char *header_pattern = "^[a-zA-Z0-9.-]{1,128}: [ -~]{1,128}$";
        if (regcomp(&header_regex, header_pattern, REG_EXTENDED) != 0) {
            send_response(client_fd, RESPONSE_500);
            close(client_fd);
            return;
        }
        if (regexec(&header_regex, header_line, 0, NULL, 0) != 0) {
            regfree(&header_regex);
            send_response(client_fd, RESPONSE_400);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        regfree(&header_regex);
    }

    /* Convert URI to file path. */
    if (uri[0] != '/') {
        send_response(client_fd, RESPONSE_400);
        close(client_fd);
        if (req_id)
            free(req_id);
        return;
    }
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s", uri + 1);

    int status_code = 0;
    /* Get the per-file lock (used by both GET and PUT). */
    rwlock_t *lock = get_file_lock(filepath);
    if (lock == NULL) {
        send_response(client_fd, RESPONSE_500);
        close(client_fd);
        if (req_id)
            free(req_id);
        return;
    }

    if (strcmp(method, "GET") == 0) {
        /* Acquire reader lock for GET. */
        reader_lock(lock);
        struct stat statbuf;
        if (stat(filepath, &statbuf) == -1) {
            send_response(client_fd, RESPONSE_404);
            status_code = 404;
            /* Log audit entry while still holding the reader lock. */
            write_audit_log("GET", uri, status_code, req_id);
            reader_unlock(lock);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        if (!S_ISREG(statbuf.st_mode)) {
            send_response(client_fd, RESPONSE_403);
            status_code = 403;
            write_audit_log("GET", uri, status_code, req_id);
            reader_unlock(lock);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        int file_fd = open(filepath, O_RDONLY);
        if (file_fd == -1) {
            if (errno == EACCES)
                send_response(client_fd, RESPONSE_403);
            else
                send_response(client_fd, RESPONSE_500);
            status_code = (errno == EACCES) ? 403 : 500;
            write_audit_log("GET", uri, status_code, req_id);
            reader_unlock(lock);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        char header_buf[256];
        snprintf(header_buf, sizeof(header_buf),
                 "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n",
                 (long)statbuf.st_size);
        if (send_response(client_fd, header_buf) < 0) {
            close(file_fd);
            write_audit_log("GET", uri, 500, req_id);
            reader_unlock(lock);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        char buf[1024];
        ssize_t bytes_read;
        while ((bytes_read = read(file_fd, buf, sizeof(buf))) > 0) {
            if (write_n_bytes(client_fd, buf, bytes_read) != bytes_read)
                break;
        }
        close(file_fd);
        status_code = 200;
        /* Write audit log while still holding the reader lock */
        write_audit_log("GET", uri, status_code, req_id);
        reader_unlock(lock);
    } else if (strcmp(method, "PUT") == 0) {
        /* Extract Content-Length header from orig_request. */
        char *cl_ptr = strstr(orig_request, "Content-Length:");
        if (cl_ptr == NULL) {
            send_response(client_fd, RESPONSE_400);
            status_code = 400;
            write_audit_log("PUT", uri, status_code, req_id);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        regex_t cl_regex;
        if (regcomp(&cl_regex, CONTENT_LENGTH_REGEX, REG_EXTENDED) != 0) {
            send_response(client_fd, RESPONSE_500);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        regmatch_t pmatch[2];
        if (regexec(&cl_regex, cl_ptr, 2, pmatch, 0) == REG_NOMATCH) {
            regfree(&cl_regex);
            send_response(client_fd, RESPONSE_400);
            status_code = 400;
            write_audit_log("PUT", uri, status_code, req_id);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        int num_digits = pmatch[1].rm_eo - pmatch[1].rm_so;
        char content_len_str[32];
        if (num_digits >= (int)sizeof(content_len_str)) {
            regfree(&cl_regex);
            send_response(client_fd, RESPONSE_400);
            status_code = 400;
            write_audit_log("PUT", uri, status_code, req_id);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        strncpy(content_len_str, cl_ptr + pmatch[1].rm_so, num_digits);
        content_len_str[num_digits] = '\0';
        long content_length = atol(content_len_str);
        regfree(&cl_regex);
        if (content_length < 0) {
            send_response(client_fd, RESPONSE_400);
            status_code = 400;
            write_audit_log("PUT", uri, status_code, req_id);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }

        /* Read the entire body into a temporary buffer before acquiring the lock */
        char *body_buffer = malloc(content_length);
        if (body_buffer == NULL) {
            send_response(client_fd, RESPONSE_500);
            status_code = 500;
            write_audit_log("PUT", uri, status_code, req_id);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        ssize_t body_bytes_read = 0;
        /* Determine if some of the body is already in orig_request */
        char *body_start = strstr(orig_request, "\r\n\r\n");
        if (body_start != NULL) {
            body_start += 4;
            ssize_t already = req_len - (body_start - orig_request);
            if (already > 0) {
                memcpy(body_buffer, body_start, already);
                body_bytes_read = already;
            }
        }
        while (body_bytes_read < content_length) {
            ssize_t n = read_n_bytes(client_fd, body_buffer + body_bytes_read,
                                     content_length - body_bytes_read);
            if (n <= 0) {
                free(body_buffer);
                send_response(client_fd, RESPONSE_500);
                status_code = 500;
                write_audit_log("PUT", uri, status_code, req_id);
                close(client_fd);
                if (req_id)
                    free(req_id);
                return;
            }
            body_bytes_read += n;
        }

        /* Now acquire the file’s writer lock and write the data.
         * Hold the writer lock until after sending the response and writing the audit log.
         */
        rwlock_t *put_lock = get_file_lock(filepath);
        if (put_lock == NULL) {
            free(body_buffer);
            send_response(client_fd, RESPONSE_500);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        writer_lock(put_lock);

        bool file_exists = false;
        struct stat file_stat;
        if (stat(filepath, &file_stat) == 0) {
            if (!S_ISREG(file_stat.st_mode)) {
                send_response(client_fd, RESPONSE_403);
                status_code = 403;
                write_audit_log("PUT", uri, status_code, req_id);
                writer_unlock(put_lock);
                free(body_buffer);
                close(client_fd);
                if (req_id)
                    free(req_id);
                return;
            }
            file_exists = true;
        }
        int file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file_fd == -1) {
            if (errno == EACCES)
                send_response(client_fd, RESPONSE_403);
            else
                send_response(client_fd, RESPONSE_500);
            status_code = (errno == EACCES) ? 403 : 500;
            write_audit_log("PUT", uri, status_code, req_id);
            writer_unlock(put_lock);
            free(body_buffer);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        ssize_t written = write_n_bytes(file_fd, body_buffer, content_length);
        free(body_buffer);
        if (written != content_length) {
            close(file_fd);
            send_response(client_fd, RESPONSE_500);
            status_code = 500;
            write_audit_log("PUT", uri, status_code, req_id);
            writer_unlock(put_lock);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        close(file_fd);
        char put_resp[128];
        if (file_exists) {
            snprintf(put_resp, sizeof(put_resp),
                     "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n%s", PUT_RESPONSE_OK);
            status_code = 200;
        } else {
            snprintf(put_resp, sizeof(put_resp),
                     "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\n%s", PUT_RESPONSE_CREATED);
            status_code = 201;
        }
        if (send_response(client_fd, put_resp) < 0) {
            write_audit_log("PUT", uri, 500, req_id);
            writer_unlock(put_lock);
            close(client_fd);
            if (req_id)
                free(req_id);
            return;
        }
        /* Write audit log while still holding the writer lock */
        write_audit_log("PUT", uri, status_code, req_id);
        writer_unlock(put_lock);
    }

    close(client_fd);
    if (req_id)
        free(req_id);
}

/* Worker thread: repeatedly pop a connection from the queue and process it */
void *worker_thread(void *arg) {
    (void)arg; // unused
    while (true) {
        request_item_t *item = NULL;
        if (queue_pop(request_queue, (void **)&item)) {
            if (item) {
                process_connection(item->client_fd);
                free(item);
            }
        }
    }
    return NULL;
}

/* Signal handler to clean up resources on SIGTERM. */
void sigterm_handler(int signum) {
    (void)signum;
    /* flush audit log; additional cleanup could be added. */
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    int threads = 4; // default
    int port = 0;
    int opt;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't':
            threads = atoi(optarg);
            break;
        default:
            fprintf(stderr, "Usage: %s [-t threads] <port>\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Usage: %s [-t threads] <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    port = atoi(argv[optind]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }

    /* Set up SIGTERM handler to ensure audit log durability. */
    signal(SIGTERM, sigterm_handler);

    /* Create the listener socket. */
    Listener_Socket_t *ls = ls_new(port);
    if (ls == NULL) {
        perror("Could not create listener socket");
        return EXIT_FAILURE;
    }
    printf("HTTP Server listening on port %d\n", port);

    /* Create the request queue. Capacity chosen arbitrarily. */
    request_queue = queue_new(1024);
    assert(request_queue);

    /* Create worker threads. */
    pthread_t *workers = malloc(threads * sizeof(pthread_t));
    for (int i = 0; i < threads; i++) {
        int rc = pthread_create(&workers[i], NULL, worker_thread, NULL);
        if (rc != 0) {
            fprintf(stderr, "Error creating worker thread\n");
            exit(EXIT_FAILURE);
        }
    }

    /* Dispatcher thread: accept connections and push them onto the queue. */
    while (true) {
        int client_fd = ls_accept(ls);
        if (client_fd < 0) {
            perror("Accept error");
            continue;
        }
        request_item_t *item = malloc(sizeof(request_item_t));
        if (!item) {
            close(client_fd);
            continue;
        }
        item->client_fd = client_fd;
        if (!queue_push(request_queue, item)) {
            free(item);
            close(client_fd);
        }
    }

    /* Clean-up (unreachable in this infinite loop) */
    ls_delete(&ls);
    queue_delete(&request_queue);
    free(workers);
    return EXIT_SUCCESS;
}


