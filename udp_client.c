#include <stdio.h>
#include <sys/socket.h> // for socket(), connect(), send(), recv() functions
#include <arpa/inet.h> // different address structures are declared here
#include <stdlib.h> // atoi() which convert string to integer
#include <string.h>
#include <unistd.h> // close() function
#include <math.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define PORT 8888 //The port on which to send data
#define BUF_LEN 4096
#define TIMEOUT_SEC 1

int client_socket;
struct sockaddr_in si_other;
int slen;

FILE *file_in;
long file_len;
int WINDOW_SIZE = 10;
int head;
int tail;
int base;
struct packet *pkt_queue;
int *ack_queue;
time_t *timeout_queue;
int last_packet_number;

void die(char *s){
    perror(s);
    exit(1);
}

struct ack{
    int ack_number;
};

struct packet{
    int packet_number;
    long packet_size;
    char data[BUF_LEN];
    int is_end;
};

void fire_window(void){
    int iter;
    time_t current_time = time(NULL);
    for(iter = head; ; iter = (iter + 1) % WINDOW_SIZE){
        if(ack_queue[iter] == 0 && (timeout_queue[iter] + TIMEOUT_SEC <= current_time)){// filter out new and timed-out packets
            if(timeout_queue[iter] != 0)// show timed out msg only for such packets
                printf("TIMEOUT %d\n", pkt_queue[iter].packet_number);
            sendto(client_socket, &pkt_queue[iter], sizeof(struct packet), 0, (struct sockaddr *) &si_other, slen);
            timeout_queue[iter] = current_time;
            printf("SEND PACKET %d : BASE %d\n", pkt_queue[iter].packet_number, base);
        }
        if(iter == tail) break;
    }
}

void put_one_packet_at_tail(void){
    long packet_size = (file_len > BUF_LEN) ? BUF_LEN : file_len;
    fread(pkt_queue[tail].data, packet_size, 1, file_in);
    file_len -= packet_size;
    pkt_queue[tail].packet_number = ++ last_packet_number;
    pkt_queue[tail].packet_size = packet_size;
    pkt_queue[tail].is_end = (file_len == 0) ? 1 : 0;
    ack_queue[tail] = 0;
    timeout_queue[tail] = 0;
}

void grow_window(void){
    if(file_len == 0) return;
    if(head == -1 && tail == -1){
        head = 0;
        tail = 0;
        if(base == -1) base = 0;
        put_one_packet_at_tail();
    }
    while(file_len > 0 && ((tail + 1) % WINDOW_SIZE != head)){
        tail = (tail + 1) % WINDOW_SIZE;
        put_one_packet_at_tail();
    }
}

void shrink_window(void){
    if(head == -1 && tail == -1) return;
    while(head != tail){
        if(ack_queue[head] == 0) break;
        base ++;
        head = (head + 1) % WINDOW_SIZE;
    }
    if(head == tail && ack_queue[head] == 1){
        head = -1;
        tail = -1;
        base ++;
    }
    grow_window();
}

void handle_ack(int ack_num){
    int put_location = (head + (ack_num - base)) % WINDOW_SIZE;
    if(head == -1 || pkt_queue[put_location].packet_number != ack_num) printf("base mismatch %d %d\n", pkt_queue[put_location].packet_number, ack_num);
    ack_queue[put_location] = 1;
    shrink_window();
    printf("RECEIVE ACK %d : BASE %d\n", ack_num, base);
}

int main(void){
    
    char file_name[100];
    struct ack recv_buf;
    struct timeval tv;
    int bytes_read;
    
    head = -1;
    tail = -1;
    base = -1;
    last_packet_number = -1;
    
    //printf("WINDOW SIZE : ");
    //scanf("%d", &WINDOW_SIZE);
    //printf("FILE NAME : ");
    //fgets(file_name, sizeof(file_name), stdin);
    
    file_in = fopen("ProblemStatement.pdf", "rb");
    fseek(file_in, 0, SEEK_END);
    file_len = ftell(file_in);
    rewind(file_in);
    
    pkt_queue = (struct packet *)malloc(sizeof(struct packet) * WINDOW_SIZE);
    ack_queue = (int *)malloc(sizeof(int) * WINDOW_SIZE);
    timeout_queue = (time_t *)malloc(sizeof(time_t) * WINDOW_SIZE);
    
    slen = sizeof(si_other);
    if((client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
        die("socket");
    }
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(PORT);
    si_other.sin_addr.s_addr = inet_addr("127.0.0.1");
    // network set up
    
    //set socket timer
    tv.tv_sec = 0;
    tv.tv_usec = 10 * 1000;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
    
    //send window_size
    sendto(client_socket, &WINDOW_SIZE, sizeof(int), 0, (struct sockaddr *) &si_other, slen);
    
    //zero out all the ACKs
    memset((char *)ack_queue, 0, sizeof(int) * WINDOW_SIZE);
    
    printf("START TRANSMISSION\n\n");
    
    grow_window();
    fire_window();
    
    while(1){
        bytes_read = recvfrom(client_socket, &recv_buf, sizeof(struct ack), 0, (struct sockaddr *) &si_other, &slen);
        if(bytes_read >= 0){
            handle_ack(recv_buf.ack_number);
        }
        if(head == -1 && tail == -1) break;
        fire_window();
    }
    
    printf("\nEND TRANSMISSION\n\n");
    
    fclose(file_in);
    close(client_socket);
}

