#include "../utils.h"

int servSock, shm_fd_field, shm_fd_buff, shm_fd_ptr;
int period;
int x_size, y_size;
cell *field;

char *sh_buff;
int *sh_pointer;
sem_t *sem_buff;

sig_atomic_t is_working1, is_working2;

void stop() {
    sem_close(sem_buff);
    sem_unlink(SEM_BUFF);
    for (int y = 0; y < y_size; ++y) {
        for (int x = 0; x < x_size; ++x) {
            sem_close(&(field[y * x_size + x].semaphore));
            char sem_name[21];
            snprintf(sem_name, 19, "/sem_%d_%d", y, x);
            sem_unlink(sem_name);
        }
    }
    close(shm_fd_field);
    munmap(field, x_size * y_size * sizeof(cell));
    shm_unlink(MEM);
    close(shm_fd_buff);
    munmap(sh_buff, BUFF_LEN*sizeof(char));
    shm_unlink(BUFF);
    close(shm_fd_ptr);
    munmap(sh_pointer, sizeof(int*));
    shm_unlink(PTR);
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
        snprintf(outpMsg, 1000, "%d gardener finished working\n", gardener == FIRST_GARDENER ? 1 : 2);
        if (gardener == FIRST_GARDENER){
            is_working1 = 0;
        } else if (gardener == SECOND_GARDENER) {
            is_working2 = 0;
        }
        puts(outpMsg);
        sem_wait(sem_buff);
        memcpy(sh_buff + *sh_pointer, outpMsg, strlen(outpMsg));
        *sh_pointer += strlen(outpMsg);
        sem_post(sem_buff);
        close(clntSocket);
        exit(0);
    }
    puts(outpMsg);
    sem_wait(sem_buff);
    memcpy(sh_buff + *sh_pointer, outpMsg, strlen(outpMsg));
    *sh_pointer += strlen(outpMsg);
    sem_post(sem_buff);
}

void ProcessMain(int childServSock) {
    int clntSock = AcceptTCPConnection(childServSock);
    printf("with child process: %d\n", (unsigned int) getpid());
    while (1) {
        HandleTCPClient(clntSock);
    }
}

void init_sharedMem(){
    shm_fd_field = shm_open(MEM, O_CREAT | O_RDWR, 0666);
    if (shm_fd_field == -1) {
        perror("\nAn error occurred while opennig shared memory\n");
        exit(0);
    }
    if (ftruncate(shm_fd_field, x_size * y_size * sizeof(cell)) == -1) {
        perror("\nAn error occurred while truncating");
        exit(0);
    }
    field = mmap(NULL, x_size * y_size * sizeof(cell), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_field, 0);
    if (field == MAP_FAILED) {
        perror("\nAn error occured while mapping shared memory");
        exit(0);
    }
    shm_fd_buff = shm_open(BUFF, O_CREAT | O_RDWR, 0666);
    if (shm_fd_buff == -1) {
        perror("\nAn error occurred while opennig shared memory\n");
        exit(0);
    }
    if (ftruncate(shm_fd_buff, BUFF_LEN * sizeof(char)) == -1) {
        perror("\nAn error occurred while truncating");
        exit(0);
    }
    sh_buff = mmap(NULL, BUFF_LEN * sizeof(char), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_buff, 0);
    if (sh_buff == MAP_FAILED) {
        perror("\nAn error occured while mapping shared memory");
        exit(0);
    }

    shm_fd_ptr = shm_open(PTR, O_CREAT | O_RDWR, 0666);
    if (shm_fd_ptr == -1) {
        perror("\nAn error occurred while opennig shared memory\n");
        exit(0);
    }
    if (ftruncate(shm_fd_ptr, sizeof(int*)) == -1) {
        perror("\nAn error occurred while truncating");
        exit(0);
    }
    sh_pointer = mmap(NULL, sizeof(int*), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_ptr, 0);
    if (sh_pointer == MAP_FAILED) {
        perror("\nAn error occured while mapping shared memory");
        exit(0);
    }
    for (int y = 0; y < y_size; ++y) {
        for (int x = 0; x < x_size; ++x) {
            char sem_name[21];
            snprintf(sem_name, 19, "/sem_%d_%d", y, x);
            field[y * x_size + x].semaphore = *sem_open(sem_name, O_CREAT, 0666, 1);
            field[y * x_size + x].state = EMPTY;
        }
    }
    if ((sem_buff = sem_open(SEM_BUFF, O_CREAT, 0666, 1)) == SEM_FAILED) {
        perror("Smth wrong with sem_open call \n");
        exit(0);
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

    init_sharedMem();
    
    *sh_pointer = 0;
    is_working1 = 1;
    is_working2 = 1;
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
    
    if ((processID = fork()) < 0) {
        perror("fork() failed");
        return 1;
    } else if (processID == 0) {
        signal(SIGINT, prev);
        int obsClntSock = AcceptTCPConnection(servSock);
        printf("Observer connected\n");
        while (is_working1 || is_working2){
            sem_wait(sem_buff);
            if (*sh_pointer > 0) {
                if (send(obsClntSock, sh_buff, *sh_pointer, 0) != *sh_pointer) {
                    perror("send() obs failed\n");
                    exit(0);
                }
                *sh_pointer = 0;
            }
            sem_post(sem_buff);
        };
        close(obsClntSock);
        exit(0);
    }

    int status;
    while ((wait(&status) > 0));
    stop();
}