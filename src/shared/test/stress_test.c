#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define THREADS 500

static int success = 0;
static int fail = 0;

static pthread_mutex_t mutex;

static void count_result(int res) {
    pthread_mutex_lock(&mutex);
    if (res) {
        success++;
    } else {
        fail++;
    }
    pthread_mutex_unlock(&mutex);
}

static int connect_to_proxy(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0){
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1080);
    if(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    // HANDSHAKE
    unsigned char hello[] = {0x05, 0x01, 0x02};
    uint8_t response[2];

    if (send(fd, hello, sizeof(hello), 0) < 0) {
        close(fd);
        return -1;
    }
    if (recv(fd, response, sizeof(response), 0) < 0) {
        close(fd);
        return -1;
    }
    if (response[0] != 0x05 || response[1] != 0x02) { //VER, METHOD
        close(fd);
        return -1;
    }

    // AUTHENTICATION
    uint8_t auth[] = {
        0x01,
        6, 'p', 'r', 'o', 't', 'o', 's',
        6, 'p', 'r', 'o', 't', 'o', 's'
    };

    if (send(fd, auth, sizeof(auth), 0) < 0){
        close(fd);
        return -1;
    }
    if (recv(fd, response, sizeof(response), 0) < 0) {
        close(fd);
        return -1;
    }
    if (response[0] != 0x01 || response[1] != 0x00) { //VER, STATUS
        close(fd);
        return -1;
    }

    //CONNECT
    uint8_t request[10];
    request[0] = 0x05;
    request[1] = 0x01;
    request[2] = 0x00;
    request[3] = 0x01;

    if(inet_pton(AF_INET, "127.0.0.1", &request[4]) <= 0) {
        close(fd);
        return -1;
    }

    request[8] = (9000 >> 8) & 0xff;
    request[9] = 9000 & 0xff;

    if (send(fd, request, sizeof(request), 0) < 0) {
        close(fd);
        return -1;
    }
    uint8_t connect_response[10];
    if (recv(fd, connect_response, sizeof(connect_response), 0) < 0) {
        close(fd);
        return -1;
    }
    if (connect_response[0] != 0x05 || connect_response[1] != 0x00) { //VER, REP
        close(fd);
        return -1;
    }

    return fd;
}


static void sleep_ms(long milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}


static void * worker(void *arg) {
    int id = *(int *)arg;

    // reparte conexiones en 200ms [VER]
    sleep_ms(id % 200);

    int fd = connect_to_proxy();
    int res = 0;
    if (fd >= 0) {
        res = 1;
        sleep(10);
        close(fd);
    }
    count_result(res);
    return NULL;
}

int main(void) {
    pthread_t threads[THREADS];
    int ids[THREADS];
    pthread_mutex_init(&mutex, NULL);

    for (int i = 0; i < THREADS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, worker, &ids[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("success=%d fail=%d total=%d\n", success, fail, success + fail);

    pthread_mutex_destroy(&mutex);
    return fail != 0;
}
