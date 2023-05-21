#include "../utils.h"

int servSock, shm_fd;
int period;
int x_size, y_size;
cell *field;

void stop() {
    for (int y = 0; y < y_size; ++y) {
        for (int x = 0; x < x_size; ++x) {
            sem_close(&(field[y * x_size + x].semaphore));
            char sem_name[21];
            snprintf(sem_name, 19, "/sem_%d_%d", y, x);
            sem_unlink(sem_name);
        }
    }
    close(shm_fd);
    munmap(field, x_size * y_size * sizeof(cell));
    shm_unlink(MEM);
    close(servSock);
    printf("Deallocated\n");
}

void handle_sigint(int sig) {
    stop();
    exit(0);
}

int AcceptTCPConnection(int childServSock) {
    int clntSock;
    struct sockaddr_in echoClntAddr;
    unsigned int clntLen;
    clntLen = sizeof(echoClntAddr);

    if ((clntSock = accept(childServSock, (struct sockaddr *) &echoClntAddr, &clntLen)) < 0) {
        perror("accept() failed");
        exit(0);
    }
    printf("Handling client %s\n", inet_ntoa(echoClntAddr.sin_addr));

    return clntSock;
}

int CreateTCPServerSocket(unsigned short port) {
    int sock;
    struct sockaddr_in echoServAddr;

    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket() failed");
        exit(0);
    }

    memset(&echoServAddr, 0, sizeof(echoServAddr));
    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    echoServAddr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0) {
        perror("bind() failed\n");
        exit(0);
    }
    if (listen(sock, MAXPENDING) < 0) {
        perror("listen() failed\n");
        exit(0);
    }
    return sock;
}

void HandleTCPClient(int clntSocket) {
    char rcvBuff[16];
    int recvMsgSize;
    char c;
    char outpMsg[1001];
    int init_values[3] = {x_size, y_size, period};
    if ((recvMsgSize = recv(clntSocket, rcvBuff, 13, 0)) < 0) {
        perror("recv() loop failed");
        exit(0);
    }
    if (recvMsgSize == 1 && rcvBuff[0] == ADD_GARDENER) {
        snprintf(outpMsg, 1000, "Gardener connected\n");
        if (send(clntSocket, init_values, sizeof(init_values), 0) != sizeof(init_values)) {
            perror("send() init failed");
            exit(0);
        }
    } else if (recvMsgSize == 13 && rcvBuff[0] == GARDENER_START) {
        char res = EMPTY;
        struct {
          int x;
          int y;
          int gardener;
        } recv_values;
        memcpy(&recv_values, rcvBuff + 1, sizeof(recv_values));
        snprintf(outpMsg,
                 1000,
                 "%d work with y=%d; x=%d",
                 recv_values.gardener == FIRST_GARDENER ? 1 : 2,
                 recv_values.y,
                 recv_values.x);
        sem_wait(&(field[recv_values.y * x_size + recv_values.x].semaphore));
        if (field[recv_values.y * x_size + recv_values.x].state == EMPTY) {
            field[recv_values.y * x_size + recv_values.x].state = recv_values.gardener;
        } else {
            res = field[recv_values.y * x_size + recv_values.x].state;
        }
        if (send(clntSocket, &res, 1, 0) != 1) {
            perror("send() start failed");
            exit(0);
        }
    } else if (recvMsgSize == 13 && rcvBuff[0] == GARDENER_FINISH) {
        output_field(outpMsg, y_size, x_size, field);
        struct {
          int x;
          int y;
          int gardener;
        } recv_values;
        memcpy(&recv_values, rcvBuff + 1, sizeof(recv_values));
        sem_post(&(field[recv_values.y * x_size + recv_values.x].semaphore));

    } else if ((recvMsgSize == 2) && (rcvBuff[0] == END_GARDENER)) {
        char gardener = rcvBuff[1];
        snprintf(outpMsg, 1000, "%d gardener finished working", gardener == FIRST_GARDENER ? 1 : 2);
        puts(outpMsg);
        exit(0);
    }
    puts(outpMsg);
}

void ProcessMain(int childServSock) {
    int clntSock = AcceptTCPConnection(childServSock);
    printf("with child process: %d\n", (unsigned int) getpid());
    while (1) {
        HandleTCPClient(clntSock);
    }
}

int main(int argc, char *argv[]) {
    prev = signal(SIGINT, handle_sigint);

    if (argc != 2) {
        fprintf(stderr, "Usage:  %s <SERVER PORT>\n", argv[0]);
        exit(1);
    }
    printf("Введите сколько мс будет принято за единицу отсчета: ");
    scanf("%d", &period);
    if (period < 1) {
        perror("Incorrect input. Finished...");
        return 0;
    }

    printf("Введите размер поля по двум координатам: ");
    scanf("%d %d", &x_size, &y_size);
    if ((x_size < 1) || (y_size < 1)) {
        perror("Incorrect input. Finished...");
        return 1;
    }

    shm_fd = shm_open(MEM, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("\nAn error occurred while opennig shared memory\n");
        return 1;
    }

    if (ftruncate(shm_fd, x_size * y_size * sizeof(cell)) == -1) {
        perror("\nAn error occurred while truncating");
        return 1;
    }
    field = mmap(NULL, x_size * y_size * sizeof(cell), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (field == MAP_FAILED) {
        perror("\nAn error occured while mapping shared memory");
        return 1;
    }

    for (int y = 0; y < y_size; ++y) {
        for (int x = 0; x < x_size; ++x) {
            char sem_name[21];
            snprintf(sem_name, 19, "/sem_%d_%d", y, x);
            field[y * x_size + x].semaphore = *sem_open(sem_name, O_CREAT, 0666, 1);
            field[y * x_size + x].state = EMPTY;
        }
    }

    int amount_obst = (int) ((0.1 * x_size * y_size + rand() % x_size * y_size * 0.2));
    while (amount_obst > 0) {
        int x = rand() % x_size;
        int y = rand() % y_size;
        if (field[y * x_size + x].state == EMPTY) {
            field[y * x_size + x].state = OBSTACLE;
            --amount_obst;
        }
    }

    unsigned short echoServPort;
    pid_t processID;

    echoServPort = atoi(argv[1]);
    servSock = CreateTCPServerSocket(echoServPort);

    if ((processID = fork()) < 0) {
        perror("fork() failed");
        return 1;
    } else if (processID == 0) {
        signal(SIGINT, prev);
        ProcessMain(servSock); // gardener 1
        exit(0);
    }

    if ((processID = fork()) < 0) {
        perror("fork() failed");
        return 1;
    } else if (processID == 0) {
        signal(SIGINT, prev);
        ProcessMain(servSock); // gardener 2
        exit(0);
    }

    int status;
    while (wait(&status) > 0);
    stop();
}