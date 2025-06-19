#include "client_socket.h"
#include "iowrapper.h"
#include "listener_socket.h"
#include "prequest.h"
#include "a5protocol.h"

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <regex.h>

/* =========================
 * Begin inline cache code
 * ========================= */

typedef enum { NO_CACHE, FIFO, LRU } cache_policy_t;

cache_policy_t active_policy = NO_CACHE; // Set in main based on argv[2]
size_t max_items = 0;
uint64_t cache_tick = 0;

typedef struct cache_item {
    char *host;
    int port;
    char *uri;
    char *full_response;
    size_t response_len;
    uint64_t timestamp; // For FIFO eviction.
    uint64_t last_used; // For LRU eviction.
    struct cache_item *next;
} cache_item_t;

#define MAX_CACHE_ENTRY (1 << 20)

static cache_item_t *cache_head = NULL;
static size_t cache_count = 0;

// Free all cache items.
void cache_cleanup(void) {
    cache_item_t *curr = cache_head;
    while (curr) {
        cache_item_t *next = curr->next;
        free(curr->host);
        free(curr->uri);
        free(curr->full_response);
        free(curr);
        curr = next;
    }
    cache_head = NULL;
    cache_count = 0;
}

// Search the cache for an entry matching host, port, and uri.
cache_item_t *cache_find(const char *host, int port, const char *uri) {
    cache_item_t *curr = cache_head;
    while (curr) {
        if (strcmp(curr->host, host) == 0 && curr->port == port && strcmp(curr->uri, uri) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

// Insert a new cache entry; if the cache is full, evict one per the active policy.
void cache_insert(
    const char *host, int port, const char *uri, const char *response, size_t response_len) {
    if (active_policy == NO_CACHE)
        return;

    // If the cache is full, evict one entry.
    if (cache_count >= max_items) {
        cache_item_t *prev = NULL, *curr = cache_head;
        cache_item_t *evict = cache_head;
        cache_item_t *evict_prev = NULL;
        if (active_policy == FIFO) {
            // Evict the oldest entry (lowest timestamp)
            uint64_t min_ts = evict->timestamp;
            while (curr) {
                if (curr->timestamp < min_ts) {
                    min_ts = curr->timestamp;
                    evict = curr;
                    evict_prev = prev;
                }
                prev = curr;
                curr = curr->next;
            }
        } else if (active_policy == LRU) {
            // Evict the least recently used (lowest last_used)
            uint64_t min_lu = evict->last_used;
            while (curr) {
                if (curr->last_used < min_lu) {
                    min_lu = curr->last_used;
                    evict = curr;
                    evict_prev = prev;
                }
                prev = curr;
                curr = curr->next;
            }
        }
        // Remove the evicted item.
        if (evict_prev == NULL)
            cache_head = evict->next;
        else
            evict_prev->next = evict->next;
        free(evict->host);
        free(evict->uri);
        free(evict->full_response);
        free(evict);
        cache_count--;
    }

    // Create a new cache item.
    cache_item_t *item = malloc(sizeof(cache_item_t));
    if (!item) {
        perror("malloc failed for cache item");
        return;
    }
    item->host = strdup(host);
    item->uri = strdup(uri);
    item->port = port;
    item->full_response = malloc(response_len + 1);
    if (!item->host || !item->uri || !item->full_response) {
        perror("malloc/strdup failed for cache item fields");
        free(item->host);
        free(item->uri);
        free(item->full_response);
        free(item);
        return;
    }
    memcpy(item->full_response, response, response_len);
    item->full_response[response_len] = '\0';
    item->response_len = response_len;
    item->timestamp = cache_tick;
    item->last_used = cache_tick;
    cache_tick++;

    // Insert the new item at the head of the list.
    item->next = cache_head;
    cache_head = item;
    cache_count++;
}

// Inject the header "Cached: True\r\n" into a cached response.
// The header is inserted right after the first CRLF in the response.
char *cache_inject(const char *full_response, size_t response_len, size_t *new_len) {
    const char *injection = "Cached: True\r\n";
    size_t injection_len = strlen(injection);
    const char *pos = strstr(full_response, "\r\n");
    if (!pos) {
        // No CRLF found; prepend the injection.
        *new_len = response_len + injection_len;
        char *new_resp = malloc(*new_len + 1);
        if (!new_resp) {
            perror("malloc failed in cache_inject");
            return NULL;
        }
        strcpy(new_resp, injection);
        strcat(new_resp, full_response);
        return new_resp;
    }
    size_t prefix_len = pos - full_response + 2; // include the CRLF
    *new_len = response_len + injection_len;
    char *new_resp = malloc(*new_len + 1);
    if (!new_resp) {
        perror("malloc failed in cache_inject");
        return NULL;
    }
    memcpy(new_resp, full_response, prefix_len);
    memcpy(new_resp + prefix_len, injection, injection_len);
    memcpy(new_resp + prefix_len + injection_len, full_response + prefix_len,
        response_len - prefix_len);
    new_resp[*new_len] = '\0';
    return new_resp;
}

/* =========================
 * End inline cache code
 * ========================= */

// Global listener socket pointer.
Listener_Socket_t *sock = NULL;

void cleanup(void) {
    if (sock) {
        ls_delete(&sock);
        sock = NULL;
    }
    cache_cleanup();
}

void handle_connection(uintptr_t connfd);
void forward_request(char *host, char *uri, int port, int client_fd);

void usage(FILE *stream, char *exec) {
    fprintf(stream, "usage: %s <port> <mode> <n>\n", exec);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Invalid Argument\n");
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    long p = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || p < 1 || p > 65535) {
        fprintf(stderr, "Invalid Argument\n");
        return EXIT_FAILURE;
    }
    int port = (int) p;

    char *mode = argv[2];
    if (strcmp(mode, "FIFO") == 0)
        active_policy = FIFO;
    else if (strcmp(mode, "LRU") == 0)
        active_policy = LRU;
    else {
        fprintf(stderr, "Invalid Argument\n");
        return EXIT_FAILURE;
    }

    long nlong = strtol(argv[3], &endptr, 10);
    if (*endptr != '\0' || nlong < 0 || nlong > 1024) {
        fprintf(stderr, "Invalid Argument\n");
        return EXIT_FAILURE;
    }
    max_items = (size_t) nlong;
    if (max_items == 0)
        active_policy = NO_CACHE;

    atexit(cleanup);
    sock = ls_new(port);
    if (!sock) {
        fprintf(stderr, "Failed to create listener socket\n");
        return EXIT_FAILURE;
    }

    while (1) {
        uintptr_t connfd = ls_accept(sock);
        if ((int) connfd < 0) {
            perror("ls_accept failed");
            continue;
        }
        handle_connection(connfd);
    }
    return EXIT_SUCCESS;
}

void handle_connection(uintptr_t connfd) {
    Prequest_t *preq = prequest_new((int) connfd);
    if (!preq) {
        close((int) connfd);
        return;
    }
    char *temp = prequest_get_host(preq);
    if (!temp) {
        perror("prequest_get_host failed");
        prequest_delete(&preq);
        close((int) connfd);
        return;
    }
    char *host_copy = strdup(temp);
    if (!host_copy) {
        perror("strdup failed for host");
        prequest_delete(&preq);
        close((int) connfd);
        return;
    }
    char *temp2 = prequest_get_uri(preq);
    if (!temp2) {
        perror("prequest_get_uri failed");
        free(host_copy);
        prequest_delete(&preq);
        close((int) connfd);
        return;
    }
    char *uri_copy = strdup(temp2);
    if (!uri_copy) {
        perror("strdup failed for uri");
        free(host_copy);
        prequest_delete(&preq);
        close((int) connfd);
        return;
    }
    int32_t req_port = prequest_get_port(preq);
    fprintf(stderr, "host=%s, port=%d, uri=%s\n", host_copy, req_port, uri_copy);

    if (max_items != 0 && active_policy != NO_CACHE) {
        cache_item_t *entry = cache_find(host_copy, req_port, uri_copy);
        if (entry) {
            if (active_policy == LRU)
                entry->last_used = cache_tick++;
            size_t new_len;
            char *mod_resp = cache_inject(entry->full_response, entry->response_len, &new_len);
            if (mod_resp)
                write_n_bytes((int) connfd, mod_resp, new_len);
            free(mod_resp);
            free(host_copy);
            free(uri_copy);
            prequest_delete(&preq);
            close((int) connfd);
            return;
        }
    }

    int server_fd = cs_new(host_copy, req_port);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to connect to %s:%d\n", host_copy, req_port);
        free(host_copy);
        free(uri_copy);
        prequest_delete(&preq);
        close((int) connfd);
        return;
    }

    char host_hdr[(MAX_HOST_SIZE + MAX_PORT_SIZE + 2) * 10];
    if (req_port != 80)
        snprintf(host_hdr, sizeof(host_hdr), "%s:%d", host_copy, req_port);
    else
        snprintf(host_hdr, sizeof(host_hdr), "%s", host_copy);

    char fmt_uri[MAX_URI_SIZE * 10];
    if (uri_copy) {
        if (uri_copy[0] == '/')
            snprintf(fmt_uri, sizeof(fmt_uri), "%s", uri_copy + 1);
        else
            snprintf(fmt_uri, sizeof(fmt_uri), "%s", uri_copy);
    } else {
        fmt_uri[0] = '\0';
    }

    char request[MAX_HEADER_LEN];
    int len = snprintf(request, MAX_HEADER_LEN,
        "GET /%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: httpproxy/1.0\r\n"
        "Connection: close\r\n\r\n",
        fmt_uri, host_hdr);
    fprintf(stderr, "Request being sent to server:\n%s\n", request);
    if (write_n_bytes(server_fd, request, len) != len) {
        fprintf(stderr, "Failed to send request to server\n");
        close(server_fd);
        free(host_copy);
        free(uri_copy);
        prequest_delete(&preq);
        close((int) connfd);
        return;
    }

    size_t total = 0;
    ssize_t nread;
    size_t buf_cap = 4096;
    char *server_resp = malloc(buf_cap);
    if (!server_resp) {
        perror("malloc failed");
        close(server_fd);
        free(host_copy);
        free(uri_copy);
        prequest_delete(&preq);
        close((int) connfd);
        return;
    }
    char buf[4096];
    int forward_err = 0;
    while (1) {
        nread = read_n_bytes(server_fd, buf, sizeof(buf));
        if (nread < 0) {
            perror("Error reading from server");
            forward_err = 1;
            break;
        }
        if (nread == 0)
            break;
        if (total + nread >= buf_cap) {
            buf_cap = total + nread + 1;
            char *tmp = realloc(server_resp, buf_cap);
            if (!tmp) {
                perror("realloc failed");
                forward_err = 1;
                break;
            }
            server_resp = tmp;
        }
        memcpy(server_resp + total, buf, nread);
        total += nread;
        if (write_n_bytes((int) connfd, buf, nread) != nread) {
            fprintf(stderr, "Failed to forward response to client\n");
            forward_err = 1;
            break;
        }
    }
    server_resp[total] = '\0';
    close(server_fd);

    // Always free the temporary server response buffer.
    if (!forward_err && total <= MAX_CACHE_ENTRY)
        cache_insert(host_copy, req_port, uri_copy, server_resp, total);
    free(server_resp);

    free(host_copy);
    free(uri_copy);
    prequest_delete(&preq);
    close((int) connfd);
}

void forward_request(char *host, char *uri, int port, int client_fd) {
    int server_fd = cs_new(host, port);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to connect to server %s:%d\n", host, port);
        return;
    }
    char host_hdr[(MAX_HOST_SIZE + MAX_PORT_SIZE + 2) * 10];
    if (port != 80)
        snprintf(host_hdr, sizeof(host_hdr), "%s:%d", host, port);
    else
        snprintf(host_hdr, sizeof(host_hdr), "%s", host);

    char fmt_uri[MAX_URI_SIZE * 10];
    if (uri) {
        if (uri[0] == '/')
            snprintf(fmt_uri, sizeof(fmt_uri), "%s", uri + 1);
        else
            snprintf(fmt_uri, sizeof(fmt_uri), "%s", uri);
    } else {
        fmt_uri[0] = '\0';
    }

    char request[MAX_HEADER_LEN];
    int len = snprintf(request, MAX_HEADER_LEN,
        "GET /%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: httpproxy/1.0\r\n"
        "Connection: close\r\n\r\n",
        fmt_uri, host_hdr);
    if (write_n_bytes(server_fd, request, len) != len) {
        fprintf(stderr, "Failed to send request to server\n");
        close(server_fd);
        return;
    }
    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read_n_bytes(server_fd, buffer, sizeof(buffer))) > 0) {
        if (write_n_bytes(client_fd, buffer, bytes_read) != bytes_read) {
            fprintf(stderr, "Failed to forward response to client\n");
            break;
        }
    }
    close(server_fd);
}

