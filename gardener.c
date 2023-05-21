#include "./utils.h"

int period;
int x_size, y_size;
int sock;
int spd, period;

void work(char gardener, int y, int x) {
    char buffer[16];
    char rcvBuff[1];
    int bytesRcvd;
    struct { int x; int y; int gardener; } send_values;
    send_values.x = x;
    send_values.y = y;
    send_values.gardener = gardener;
    buffer[0] = GARDENER_START;
    memcpy(&(buffer[1]), &send_values, sizeof(send_values));
    if (send(sock, buffer, 13, 0) != 13) {
        perror("send() start failed");
        exit(0);
    }
    if ((bytesRcvd = recv(sock, rcvBuff, 1, 0)) <= 0) {
        perror("recv() start failed");
        exit(0);
    }
    if (bytesRcvd != 1) {
        perror("recv() wrong data");
        exit(0);
    }
    if (rcvBuff[0] == EMPTY) {
        usleep(spd * period * 1000);
    }
    buffer[0] = GARDENER_FINISH;
    if (send(sock, buffer, 13, 0) != 13) {
        perror("send() finish failed");
        exit(0);
    }
    usleep(period * 1000);
}

void gardener1() {
    for (int y = 0; y < y_size; ++y) {
        for (int x = y % 2 ? x_size - 1 : 0; y % 2 ? x >= 0 : x < x_size;
             y % 2 ? --x : ++x) {
            printf("1 work with y=%d; x=%d\n", y, x);
            work(FIRST_GARDENER, y, x);
        }
    }
    printf("1 all\n");
}

void gardener2() {
    for (int x = x_size - 1; x >= 0; --x) {
        for (int y = (x_size - 1 - x) % 2 ? 0 : y_size - 1;
             (x_size - 1 - x) % 2 ? y < y_size : y >= 0;
             (x_size - 1 - x) % 2 ? ++y : --y) {
            printf("2 work with y=%d; x=%d\n", y, x);
            work(SECOND_GARDENER, y, x);
        }
    }
    printf("2 all\n");
}

int main(int argc, char *argv[]) {
    struct sockaddr_in recvServAddr;
    unsigned short recvServPort;
    char *servIP;
    char rcvBuff[13];
    int bytesRcvd;
    char gardener = 0;
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <Server IP> <Port> <N> <Speed>\n", argv[0]);
        exit(1);
    }

    servIP = argv[1];
    recvServPort = atoi(argv[2]);
    gardener = atoi(argv[3]);
    spd = atoi(argv[4]);
    if (gardener == 1) {
        gardener = FIRST_GARDENER;
    } else if (gardener == 2) {
        gardener = SECOND_GARDENER;
    } else {
        perror("Uncorrect garder number");
        exit(0);
    }
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket() failed");
        exit(0);
    }

    memset(&recvServAddr, 0, sizeof(recvServAddr));
    recvServAddr.sin_family = AF_INET;
    recvServAddr.sin_addr.s_addr = inet_addr(servIP);
    recvServAddr.sin_port = htons(recvServPort);

    if (connect(sock, (struct sockaddr *) &recvServAddr, sizeof(recvServAddr)) < 0) {
        perror("connect() failed");
        exit(0);
    }

    char c = ADD_GARDENER;

    if (send(sock, &c, 1, 0) != 1) {
        perror("send() add failed");
        exit(0);
    }
    if ((bytesRcvd = recv(sock, rcvBuff, 13, 0)) <= 0) {
        perror("recv() add failed");
        exit(0);
    }

    struct { int x_size; int y_size; int period; } init_values;
    memcpy (&init_values, rcvBuff, 12);
    x_size = init_values.x_size;
    y_size = init_values.y_size;
    period = init_values.period;

    if (gardener == FIRST_GARDENER) {
        gardener1();
    } else if (gardener == SECOND_GARDENER) {
        gardener2();
    }

    char sendBuff[2];
    sendBuff[0] = END_GARDENER;
    sendBuff[1] = gardener;
    if (send(sock, sendBuff, 2, 0) != 2) {
        perror("send() end failed");
        exit(0);
    }
}