/**
 * httpserver.c
 *
 * An HTTP/1.1 server that implements GET and PUT requests.
 * It uses helper functions for robust I/O and a listener socket abstraction.
 *
 * Compile with the other provided source files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>

// Provided helper functions from asgn2_helper_funcs.h:
//    ssize_t read_n_bytes(int in, char buf[], size_t n);
//    ssize_t write_n_bytes(int out, char buf[], size_t n);
//    ssize_t pass_n_bytes(int src, int dst, size_t n);
#include "listener_socket.h"  // provides: ls_new, ls_accept, ls_delete
#include "iowrapper.h"        // provides: write_n_bytes, pass_n_bytes, read_n_bytes

// Maximum sizes for the request
#define MAX_REQUEST_SIZE 8192
#define MAX_HEADERS 128

// Our request-line must match: METHOD SP URI SP HTTP/x.y
// METHOD: [a-zA-Z]{1,8}
// URI:    /[a-zA-Z0-9.-]{1,63}
// version: HTTP/[0-9]\.[0-9]
#define REQUEST_LINE_PATTERN "^([a-zA-Z]{1,8}) (/[a-zA-Z0-9.-]{1,63}) (HTTP/[0-9]\\.[0-9])$"

// Regular expression for Content-Length header.
#define CONTENT_LENGTH_REGEX "Content-Length: ([[:digit:]]+)"

// Response definitions (exact message bodies and Content-Length values per assignment table)
#define RESPONSE_400 "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n"
#define RESPONSE_403 "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n"
#define RESPONSE_404 "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n"
#define RESPONSE_500 "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n"
#define RESPONSE_501 "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n"
#define RESPONSE_505 "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not Supported\n"

// For successful PUT responses:
#define PUT_RESPONSE_OK "OK\n"         // length 3
#define PUT_RESPONSE_CREATED "Created\n" // length 8

// read_http_request: reads bytes until "\r\n\r\n" is seen or until buffer is full.
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

// send_response: sends the entire message string.
static int send_response(int fd, const char *msg) {
    size_t len = strlen(msg);
    if (write_n_bytes(fd, (char *)msg, len) != (ssize_t)len) {
        return -1;
    }
    return 0;
}

// process_connection: handles one client connection.
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
    
    // Save an unmodified copy of the original request for use in PUT.
    char orig_request[MAX_REQUEST_SIZE];
    strncpy(orig_request, request, req_len);
    orig_request[req_len] = '\0';

    // Tokenize the request into lines.
    char *saveptr;
    char *line = strtok_r(request, "\r\n", &saveptr);
    if (line == NULL) {
        send_response(client_fd, RESPONSE_400);
        close(client_fd);
        return;
    }

    // Validate the request-line.
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

    // Check HTTP version.
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

    // Check method: must be GET or PUT.
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

    // Validate each header line.
    char *header_line;
    while ((header_line = strtok_r(NULL, "\r\n", &saveptr)) != NULL &&
           header_line[0] != '\0') {
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
            return;
        }
        regfree(&header_regex);
    }
    
    // Convert the URI to a file path (URI must begin with '/').
    if (uri[0] != '/') {
        send_response(client_fd, RESPONSE_400);
        close(client_fd);
        return;
    }
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s", uri + 1);

    if (strcmp(method, "GET") == 0) {
        // GET branch.
        struct stat statbuf;
        if (stat(filepath, &statbuf) == -1) {
            send_response(client_fd, RESPONSE_404);
            close(client_fd);
            return;
        }
        if (!S_ISREG(statbuf.st_mode)) {
            send_response(client_fd, RESPONSE_403);
            close(client_fd);
            return;
        }
        int file_fd = open(filepath, O_RDONLY);
        if (file_fd == -1) {
            if (errno == EACCES)
                send_response(client_fd, RESPONSE_403);
            else
                send_response(client_fd, RESPONSE_500);
            close(client_fd);
            return;
        }
        char header[256];
        snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", (long)statbuf.st_size);
        if (send_response(client_fd, header) < 0) {
            close(file_fd);
            close(client_fd);
            return;
        }
        char buf[1024];
        ssize_t bytes_read;
        while ((bytes_read = read(file_fd, buf, sizeof(buf))) > 0) {
            if (write_n_bytes(client_fd, buf, bytes_read) != bytes_read)
                break;
        }
        close(file_fd);
    } else if (strcmp(method, "PUT") == 0) {
        // --- Begin PUT branch ---
        // Use orig_request to extract the Content-Length header and locate the body.
        char *cl_ptr = strstr(orig_request, "Content-Length:");
        if (cl_ptr == NULL) {
            send_response(client_fd, RESPONSE_400);
            close(client_fd);
            return;
        }
        regex_t cl_regex;
        if (regcomp(&cl_regex, CONTENT_LENGTH_REGEX, REG_EXTENDED) != 0) {
            send_response(client_fd, RESPONSE_500);
            close(client_fd);
            return;
        }
        regmatch_t pmatch[2];
        if (regexec(&cl_regex, cl_ptr, 2, pmatch, 0) == REG_NOMATCH) {
            regfree(&cl_regex);
            send_response(client_fd, RESPONSE_400);
            close(client_fd);
            return;
        }
        int num_digits = pmatch[1].rm_eo - pmatch[1].rm_so;
        char content_len_str[32];
        if (num_digits >= (int)sizeof(content_len_str)) {
            regfree(&cl_regex);
            send_response(client_fd, RESPONSE_400);
            close(client_fd);
            return;
        }
        strncpy(content_len_str, cl_ptr + pmatch[1].rm_so, num_digits);
        content_len_str[num_digits] = '\0';
        long content_length = atol(content_len_str);
        regfree(&cl_regex);
        if (content_length < 0) {
            send_response(client_fd, RESPONSE_400);
            close(client_fd);
            return;
        }

        // Check if the target file already exists.
        bool file_exists = false;
        struct stat file_stat;
        if (stat(filepath, &file_stat) == 0) {
            if (!S_ISREG(file_stat.st_mode)) {
                send_response(client_fd, RESPONSE_403);
                close(client_fd);
                return;
            }
            file_exists = true;
        }

        // Determine where the headers end and the body begins in orig_request.
        char *body_start = strstr(orig_request, "\r\n\r\n");
        char *body = NULL;
        ssize_t body_bytes = 0;
        if (body_start != NULL) {
            body = body_start + 4;
            body_bytes = req_len - (body - orig_request);
        }

        // Open (or create/truncate) the file for writing.
        int file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file_fd == -1) {
            if (errno == EACCES)
                send_response(client_fd, RESPONSE_403);
            else
                send_response(client_fd, RESPONSE_500);
            close(client_fd);
            return;
        }

        // Write any body bytes already present.
        ssize_t total_written = 0;
        if (body != NULL && body_bytes > 0) {
            total_written = write_n_bytes(file_fd, body, body_bytes);
            if (total_written != body_bytes) {
                close(file_fd);
                send_response(client_fd, RESPONSE_500);
                close(client_fd);
                return;
            }
        }

        // Calculate remaining bytes to be read from the client.
        ssize_t remaining = content_length - total_written;
        if (remaining < 0) {
            close(file_fd);
            send_response(client_fd, RESPONSE_400);
            close(client_fd);
            return;
        }
        while (remaining > 0) {
            char buf[4096];
            size_t to_read = (remaining > 0 && (size_t)remaining < sizeof(buf)) ? (size_t)remaining : sizeof(buf);

            ssize_t n = read_n_bytes(client_fd, buf, to_read);
            if (n <= 0) {
                close(file_fd);
                send_response(client_fd, RESPONSE_500);
                close(client_fd);
                return;
            }
            if (write_n_bytes(file_fd, buf, n) != n) {
                close(file_fd);
                send_response(client_fd, RESPONSE_500);
                close(client_fd);
                return;
            }
            remaining -= n;
        }
        close(file_fd);
        // --- End PUT branch ---

        // Send proper response: 200 OK if file existed, 201 Created if new.
        char put_resp[128];
        if (file_exists) {
            snprintf(put_resp, sizeof(put_resp), "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n%s", PUT_RESPONSE_OK);
        } else {
            snprintf(put_resp, sizeof(put_resp), "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\n%s", PUT_RESPONSE_CREATED);
        }
        send_response(client_fd, put_resp);
    }
    close(client_fd);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }
    Listener_Socket_t *ls = ls_new(port);
    if (ls == NULL) {
        perror("Could not create listener socket");
        return EXIT_FAILURE;
    }
    printf("HTTP Server listening on port %d\n", port);
    while (true) {
        int client_fd = ls_accept(ls);
        if (client_fd < 0) {
            perror("Accept error");
            continue;
        }
        process_connection(client_fd);
    }
    ls_delete(&ls);
    return EXIT_SUCCESS;
}


