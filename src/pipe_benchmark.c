#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

#define DEFAULT_MESSAGES 100000
#define DEFAULT_SIZE 64

double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

int main(int argc, char *argv[])
{
    int num_messages = DEFAULT_MESSAGES;
    int message_size = DEFAULT_SIZE;

    if (argc >= 2)
        num_messages = atoi(argv[1]);

    if (argc >= 3)
        message_size = atoi(argv[2]);

    if (num_messages <= 0 || message_size <= 0)
    {
        fprintf(stderr, "Messages and message size must be positive.\n");
        return 1;
    }

    int fd[2];

    if (pipe(fd) == -1)
    {
        perror("pipe");
        return 1;
    }

    char *buffer = malloc(message_size);

    if (buffer == NULL)
    {
        perror("malloc");
        return 1;
    }

    memset(buffer, 'A', message_size);

    double start_time = get_time();

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        free(buffer);
        return 1;
    }

    if (pid == 0)
    {
        /* Consumer process */

        close(fd[1]);

        for (int i = 0; i < num_messages; i++)
        {
            int bytes_received = 0;

            while (bytes_received < message_size)
            {
                ssize_t bytes = read(
                    fd[0],
                    buffer + bytes_received,
                    message_size - bytes_received
                );

                if (bytes <= 0)
                {
                    perror("read");
                    free(buffer);
                    close(fd[0]);
                    exit(1);
                }

                bytes_received += bytes;
            }
        }

        close(fd[0]);
        free(buffer);

        exit(0);
    }
    else
    {
        /* Producer process */

        close(fd[0]);

        for (int i = 0; i < num_messages; i++)
        {
            int bytes_sent = 0;

            while (bytes_sent < message_size)
            {
                ssize_t bytes = write(
                    fd[1],
                    buffer + bytes_sent,
                    message_size - bytes_sent
                );

                if (bytes <= 0)
                {
                    perror("write");
                    free(buffer);
                    close(fd[1]);
                    return 1;
                }

                bytes_sent += bytes;
            }
        }

        close(fd[1]);

        waitpid(pid, NULL, 0);

        double end_time = get_time();
        double elapsed = end_time - start_time;

        double total_bytes =
            (double) num_messages * message_size;

        double throughput =
            (total_bytes / (1024.0 * 1024.0)) / elapsed;

        double messages_per_second =
            num_messages / elapsed;

        printf("\n===== PIPE IPC BENCHMARK =====\n");
        printf("Messages       : %d\n", num_messages);
        printf("Message Size   : %d bytes\n", message_size);
        printf("Total Data     : %.2f MB\n",
               total_bytes / (1024.0 * 1024.0));
        printf("Execution Time : %.6f seconds\n", elapsed);
        printf("Throughput     : %.2f MB/s\n", throughput);
        printf("Messages/sec   : %.2f\n", messages_per_second);
        printf("==============================\n");
    }

    free(buffer);

    return 0;
}
