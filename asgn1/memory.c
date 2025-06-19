#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#define DATA_SIZE 4096

// Function to process file operations
int process_file_operation(void) {
    char data_buffer[DATA_SIZE];
    ssize_t num_bytes_read;
    bool is_get = false;
    bool is_set = false;
    int file_descriptor = -1;
    int bytes_to_set = -1;
    char *input_line;
    int read_offset = 0;

    num_bytes_read = read(0, data_buffer, sizeof(data_buffer) - 1);
    if (num_bytes_read < 0) {
        fprintf(stderr, "Invalid Command\n");
        return 1;
    }

    data_buffer[num_bytes_read] = '\0';
    input_line = strtok(data_buffer, "\n");
    read_offset += strlen(input_line) + 1;

    if (strcmp(input_line, "get") == 0) {
        is_get = true;
    } else if (strcmp(input_line, "set") == 0) {
        is_set = true;
    } else {
        fprintf(stderr, "Invalid Command\n");
        return 1;
    }

    if (is_get) {
        input_line = strtok(NULL, "\n");
        if (!input_line || strtok(NULL, "\0")) {
            fprintf(stderr, "Invalid Command\n");
            return 1;
        }

        int file_read_fd = open(input_line, O_RDONLY);
        if (file_read_fd < 0) {
            fprintf(stderr, "Invalid Command\n");
            return 1;
        }

        char file_content_buffer[DATA_SIZE];
        while (
            (num_bytes_read = read(file_read_fd, file_content_buffer, sizeof(file_content_buffer)))
            > 0) {
            write(1, file_content_buffer, num_bytes_read);
        }

        if (num_bytes_read < 0) {
            fprintf(stderr, "Invalid Command\n");
            close(file_read_fd);
            return 1;
        }

        close(file_read_fd);
    } else if (is_set) {
        input_line = strtok(NULL, "\n");
        read_offset += strlen(input_line) + 1;
        if (!input_line) {
            fprintf(stderr, "Invalid Command\n");
            return 1;
        }

        file_descriptor = open(input_line, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (file_descriptor < 0) {
            fprintf(stderr, "Invalid Command\n");
            return 1;
        }

        input_line = strtok(NULL, "\n");
        read_offset += strlen(input_line) + 1;
        if (!input_line) {
            fprintf(stderr, "Invalid Command\n");
            close(file_descriptor);
            return 1;
        }

        bytes_to_set = atoi(input_line);
        if (bytes_to_set < 0) {
            fprintf(stderr, "Invalid Command\n");
            close(file_descriptor);
            return 1;
        }

        if (bytes_to_set == 0) {
            printf("OK\n");
            close(file_descriptor);
            return 0;
        }

        int written_bytes
            = write(file_descriptor, data_buffer + read_offset, num_bytes_read - read_offset);
        while (written_bytes < bytes_to_set) {
            ssize_t written = 0;
            num_bytes_read = read(STDIN_FILENO, data_buffer, DATA_SIZE);
            if (num_bytes_read == 0)
                break;
            if (num_bytes_read == -1)
                break;

            while (written < num_bytes_read) {
                ssize_t result
                    = write(file_descriptor, data_buffer + written, num_bytes_read - written);
                if (result == -1)
                    exit(1);
                written += result;
                written_bytes += result;
            }
        }

        printf("OK\n");
        close(file_descriptor);
    }
    return 0;
}

int main(void) {
    return process_file_operation();
}

