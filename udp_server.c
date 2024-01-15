#include <stdio.h>
#include <sys/socket.h> // for socket(), connect(), send(), recv() functions
#include <arpa/inet.h> // different address structures are declared here
#include <stdlib.h> // atoi() which convert string to integer
#include <string.h>
#include <unistd.h> // close() function
#include<math.h>
#include <time.h>

#define PORT 8888 //The port on which to listen for incoming data
#define BUF_LEN 4096
#define DROP_RATE 0.4 // range 0 to 1, keep higher value for more drop rate

struct packet *pkt_queue;
int *accepted_queue;
int head;
int tail;
int base;
FILE *file_out;
int WINDOW_SIZE;
int can_grow;
int last_packet;

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

void grow_window(void){
    if(can_grow == 0) return;
    //printf("inside grow\n");
    if(head == -1 && tail == -1){
        head = 0;
        tail = 0;
        if(base == -1) base = 0;
        accepted_queue[0] = 0;
    }
    while((tail + 1) % WINDOW_SIZE != head){
        tail = (tail + 1) % WINDOW_SIZE;
        accepted_queue[tail] = 0;
    }
}

void shrink_window(void){
    if(head == -1 && tail == -1) return;
    while(accepted_queue[head] == 1){
        fwrite(pkt_queue[head].data, pkt_queue[head].packet_size, 1, file_out);
        base ++;
        if(head == tail) break;
        head = (head + 1) % WINDOW_SIZE;
    }
    if(head == tail){
        head = -1;
        tail = -1;
        //printf("growing stopped\n");
    }
    grow_window();
}

void handle_receive(int pkt_num, struct packet recv_pkt){
    int put_location = (head + (pkt_num - base)) % WINDOW_SIZE;
    accepted_queue[put_location] = 1;
    pkt_queue[put_location] = recv_pkt;
    if(recv_pkt.is_end == 1){
        last_packet = recv_pkt.packet_number;
        can_grow = 0;
    }
    shrink_window();
}

int main(void){
    
    struct sockaddr_in si_me, si_other;
    int server_socket;
    int slen;
    
    long recv_len;
    struct packet recv_buf;
    struct ack send_buf;
    
    file_out = fopen("output_file", "wb");
    
    slen = sizeof(si_other);
    if((server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
        die("socket");
    }
    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    
    //bind socket to port
    if(bind(server_socket, (struct sockaddr*)&si_me, sizeof(si_me) ) == -1){
        die("bind");
    }
    fflush(stdout);
    
    // receive window size
    recvfrom(server_socket, &WINDOW_SIZE, sizeof(int), 0, (struct sockaddr *) &si_other, &slen);
    
    pkt_queue = (struct packet *)malloc(sizeof(struct packet) * WINDOW_SIZE);
    accepted_queue = (int *)malloc(sizeof(int) * WINDOW_SIZE);
    memset((char *) accepted_queue, 0, sizeof(int) * WINDOW_SIZE);
    head = -1;
    tail = -1;
    base = -1;
    can_grow = 1;
    
    printf("START RECEIVING\n");
    
    grow_window();
    
    int total_pkt = 0;
    last_packet = -2;
    while(1){
        if(total_pkt == last_packet + 1) break;
        
        if((recv_len = recvfrom(server_socket, &recv_buf, sizeof(struct packet), 0, (struct sockaddr *) &si_other, &slen)) == -1){
            die("recvfrom()");
        }
        
        printf("RECEIVE PACKET %d : ", recv_buf.packet_number);
        
        if(drand48() < DROP_RATE){
            printf("DROP : BASE %d\n", base);
            continue;
        }
        
        total_pkt ++;
        printf("ACCEPT : ");
        handle_receive(recv_buf.packet_number, recv_buf);
        printf("BASE %d\nSEND ACK %d\n", base, recv_buf.packet_number);
        
        send_buf.ack_number = recv_buf.packet_number;
        sendto(server_socket, &send_buf, sizeof(struct ack), 0, (struct sockaddr *) &si_other, slen);
        
    }
    
    printf("\nRECEIVING COMPLETE\n%d %d\n", head, tail);
    
    
    fclose(file_out);
    close(server_socket);
}

