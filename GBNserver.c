/* A simple server in the internet domain using TCP
   The port number is passed as an argument 
   This version runs forever, forking off a separate 
   process for each connection
*/
#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h>	/* for the waitpid() system call */
#include <signal.h>	/* signal name macros, and the kill() prototype */
#include <string.h>


#define DATA_SIZE 1000
#define HEAD_LEN 16
#define PKT_SIZE DATA_SIZE + HEAD_LEN

typedef struct{
    int seq;
    int ack;
    int length;
    short flg;
    short rwnd;
    char  data[DATA_SIZE];
}packet;

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void dostuff(int, struct sockaddr_in, socklen_t,int); /* function prototype */
int CheckHdr(packet*);
int CheckAck(packet*,long,int);
void error(char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
     int sockfd, portno;
     int timeout;
     int cwnd;
     socklen_t clilen;
     struct sockaddr_in serv_addr, cli_addr;
     struct sigaction sa;          // for signal SIGCHLD

     if (argc < 4) {
         fprintf(stderr,"ERROR, lack of parameter (portno,timeout,cwnd)\n");
         exit(1);
     }
     sockfd = socket(AF_INET, SOCK_DGRAM, 0);
     if (sockfd < 0) 
        error("ERROR opening socket");
     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = atoi(argv[1]);
     timeout = atoi(argv[2]);
     cwnd = atoi(argv[3]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
    

    
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              error("ERROR on binding");
     
     
     clilen = sizeof(cli_addr);
     
     /****** Kill Zombie Processes ******/
     sa.sa_handler = sigchld_handler; // reap all dead processes
     sigemptyset(&sa.sa_mask);
     sa.sa_flags = SA_RESTART;
     if (sigaction(SIGCHLD, &sa, NULL) == -1) {
         perror("sigaction");
         exit(1);
     }
     /*********************************/
     
     while (1) {
         int n,i;
         //int expect_ack=0;
         long m;
         packet* recvpkt;
         FILE* fp = NULL;
         long head,tail;
         //FILE* fp_head = NULL;
         //FILE* fp_tail = NULL;
         struct timeval tv;
         tv.tv_sec = 0;
         tv.tv_usec = timeout;
         
         fd_set rd_fds;
         FD_ZERO(&rd_fds);
         int newsock,finsock;
         
         int sgn; //indicate received message
         char startbuf[PKT_SIZE];
         bzero(startbuf,PKT_SIZE);
         char rbuffer[HEAD_LEN];
         bzero(rbuffer,HEAD_LEN);
         n = recvfrom(sockfd,startbuf,PKT_SIZE-1,0,(struct sockaddr*)&cli_addr,&clilen);
         if (n < 0) error("ERROR receiving from socket");
         printf("requesting for file: %s\n",startbuf+12);
         recvpkt = (packet*)startbuf;
         //bzero(startbuf,PKT_SIZE);
         sgn = CheckHdr(recvpkt);
         //printf("sgn= %d\n",sgn);
         //printf("flg: %d\n",recvpkt->flg);
         //printf("seq: %d\n",recvpkt->seq);
         //printf("ack: %d\n",recvpkt->ack);
         //printf("rwnd: %d\n",recvpkt->rwnd);
         if(sgn== 0) // file request msg
         {
             fp=fopen(recvpkt->data,"rb");
             if(fp==NULL)
             {
                 packet errreq;
                 errreq.seq = 0;
                 errreq.ack = 0;
                 errreq.flg = 404; //file not found
                 errreq.rwnd = 0;
                 n=sendto(sockfd,&errreq,HEAD_LEN,0,(struct sockaddr*)&cli_addr,clilen);
                 if(n<0) error("ERROR sending to socket");
                 fprintf(stderr,"File \"%s\" not found \n", recvpkt->data);
             }
             else
             {
                 //fp_tail=fp_head=fp;
                 char *buf =(char*)malloc((DATA_SIZE)*sizeof(char));
                 if(buf==NULL)
                 {
                     error("malloc buf fail\n");
                     return(-1);
                 }
                 fseek(fp,0L,SEEK_END);
                 long filelen = ftell(fp);
                 //int remain = filelen % DATA_SIZE;
                 //int pktnum = filelen / DATA_SIZE + (remain > 0);
                 fseek(fp,0L,SEEK_SET);
                 long wnd_capacity = cwnd*DATA_SIZE;
                 
                 packet firstsend; //packets in the initial window
                 firstsend.ack = 0;
                 firstsend.flg = 0;
                 firstsend.rwnd = 0;
                 //printf("here\n");
                 if(DATA_SIZE<(int)filelen)
                 {
                     if(wnd_capacity<filelen)
                  {
                     for(i=0;i<cwnd;i++) //send the initial window
                   {
                     firstsend.seq = i*DATA_SIZE ;
                     printf("send seq: %d\n",firstsend.seq);
                     m=fread(buf,1,DATA_SIZE,fp);
                     firstsend.length = m;
                     memcpy(firstsend.data,buf,DATA_SIZE);
                     //printf("send data: %s\n",firstsend.data);
                     n=sendto(sockfd,&firstsend,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                     if(n<0) error("ERROR sending to socket");
                     memset(firstsend.data,0,DATA_SIZE);
                     bzero(buf,DATA_SIZE);
                   }
                 //expect_ack += DATA_SIZE;
                   free(buf);
                   tail = ftell(fp);
                   head = tail - cwnd*DATA_SIZE;
                  } //end of if(wnd_capacity<filelen)
                     else
                     {
                         int inipkt_num = (int)filelen/DATA_SIZE + ((int)filelen % DATA_SIZE > 0);
                         for(i=0;i<inipkt_num-1;i++) //send the initial window
                         {
                             firstsend.seq = i*DATA_SIZE ;
                             printf("send seq: %d\n",firstsend.seq);
                             m=fread(buf,1,DATA_SIZE,fp);
                             firstsend.length = m;
                             memcpy(firstsend.data,buf,DATA_SIZE);
                             //printf("send data: %s\n",firstsend.data);
                             n=sendto(sockfd,&firstsend,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                             if(n<0) error("ERROR sending to socket");
                             memset(firstsend.data,0,DATA_SIZE);
                             bzero(buf,DATA_SIZE);
                         }
                         firstsend.seq = i*DATA_SIZE ;
                         firstsend.flg = 1;
                         printf("send seq: %d\n",firstsend.seq);
                         m=fread(buf,1,DATA_SIZE,fp);
                         firstsend.length = m;
                         memcpy(firstsend.data,buf,DATA_SIZE);
                         //printf("send data: %s\n",firstsend.data);
                         n=sendto(sockfd,&firstsend,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                         if(n<0) error("ERROR sending to socket");
                         memset(firstsend.data,0,DATA_SIZE);
                         bzero(buf,DATA_SIZE);
                         free(buf);
                         tail = ftell(fp);
                         head = tail - inipkt_num*DATA_SIZE;
                     }

                 }
                 else
                 {
                      m=fread(buf,1,filelen,fp);
                     firstsend.seq = 0;
                     firstsend.length = m;
                     firstsend.flg = 1;
                     memcpy(firstsend.data,buf,filelen);
                     n=sendto(sockfd,&firstsend,HEAD_LEN+filelen,0,(struct sockaddr*)&cli_addr,clilen);
                     //printf("last send size:%d\n",n);
                     if(n<0) error("ERROR sending to socket");
                     free(buf);
                 }
                 
                 //printf("head:%ld tail:%ld\n",head,tail);
                 //fseek(fp_tail,cwnd*DATA_SIZE,SEEK_CUR);
                 while(1) // receive acks
                 {
                     FD_SET (sockfd, &rd_fds);
                     newsock = select(sockfd+1,&rd_fds,NULL,NULL,&tv);
                     if(newsock==-1)
                     {
                         error("Select error!\n");
                     }
                     if(newsock)
                     {
                       n = recvfrom(sockfd,rbuffer,HEAD_LEN-1,0,(struct sockaddr*)&cli_addr,&clilen);
                       if (n < 0) error("ERROR receiving from socket");
                       
                       recvpkt = (packet*)rbuffer;
                       //bzero(rbuffer,HEAD_LEN);
                       //printf("CHeckack:%d\n",CheckAck(recvpkt,head));
                         printf("received ack num: %d\n",recvpkt->ack);
                       if(recvpkt->ack == filelen && recvpkt->flg ==5)
                       {
                           printf("file transfer completed\n");
                           fclose(fp);
                           break;
                       }
                       if(CheckAck(recvpkt,head, cwnd)>0) // receive a new ack
                       {
                           long offset = recvpkt->ack - head;
                           //printf("offset: %ld\n",offset);
                           packet newpkt;
                           newpkt.ack = 0;
                           newpkt.flg = 0;
                           newpkt.rwnd = 0;
                           head = head + offset;
                           //fseek(fp_head,offset,SEEK_CUR);
                           if(head+cwnd*DATA_SIZE>=filelen) //last packet
                           {
                               //long tlpos = ftell(fp_tail);
                               newpkt.seq = (int)tail;
                               printf("send seq: %d\n",newpkt.seq);
                               newpkt.flg = 1;
                               long lstpktlen = filelen - tail;
                               char *buf_2=(char*)malloc(lstpktlen*sizeof(char));
                               if(buf_2==NULL)
                               {
                                   error("malloc buf_2 fail\n");
                                   return (-1);
                               }
                               m=fread(buf_2,1,lstpktlen,fp);
                               newpkt.length =m;
                               memcpy(newpkt.data,buf_2,lstpktlen);
                               n=sendto(sockfd,&newpkt,HEAD_LEN+lstpktlen,0,(struct sockaddr*)&cli_addr,clilen);
                               //printf("last send size:%d\n",n);
                               if(n<0) error("ERROR sending to socket");
                               free(buf_2);
                         }
                           else
                            {
                                int newpktnum = ((int)offset)/DATA_SIZE + (((int)offset) % DATA_SIZE>0);
                                char* buf_1=(char*)malloc((DATA_SIZE)*sizeof(char));
                                if(buf_1==NULL)
                                {
                                    error("malloc buf_1 fail\n");
                                    return (-1);
                                }
                                //fread(buf_1,1,offset-1,fp_head); //read new data to buf_1
                                //fseek(fp_tail, offset, SEEK_CUR); //slide window
                                
                                int j=0;
                                for(j=0;j<newpktnum;j++)
                                {
                                    m=fread(buf_1,1,DATA_SIZE,fp);
                                    newpkt.length = m;
                                    newpkt.seq = (int)tail + j*DATA_SIZE;
                                    printf("send seq: %d\n",newpkt.seq);
                                    memcpy(newpkt.data,buf_1,DATA_SIZE);
                                    //printf("send data: %s\n",newpkt.data);
                                    n=sendto(sockfd,&newpkt,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                                    if(n<0) error("ERROR sending to socket");
                                    memset(newpkt.data,0,DATA_SIZE);
                                    bzero(buf_1,DATA_SIZE);
                                }
                               // expect_ack += DATA_SIZE;
                                tail = tail + offset;
                                free(buf_1);
                            }
                       }/*end of recv new ack*/
                       //else if(CheckAck(recvpkt,head)==1)//receive duplicate ack
                       //{
                           
                       //}/*end of recv duplicate ack*/
                        
                     }/*end of if(newsock) */
                     else //timeout
                     {
                         fseek(fp,head,SEEK_SET);
                         char *buf_3 =(char*)malloc((DATA_SIZE)*sizeof(char));
                         packet resend; //packets in the current window
                         resend.ack = 0;
                         resend.flg = 0;
                         resend.rwnd = 0;
                         //long curhd = ftell(fp);
                         if((head+cwnd*DATA_SIZE) <= filelen)
                         {
                            for(i=0;i<cwnd;i++) //resend the current window
                            {
                             resend.seq = (int)head + i*DATA_SIZE;
                             printf("resend seq: %d\n",resend.seq);
                             m=fread(buf_3,1,DATA_SIZE,fp);
                             resend.length = m;
                             memcpy(resend.data,buf_3,DATA_SIZE);
                             //printf("resend data: %s\n",resend.data);
                             n=sendto(sockfd,&resend,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                             if(n<0) error("ERROR sending to socket");
                             memset(resend.data,0,DATA_SIZE);
                             bzero(buf_3,DATA_SIZE);
                             }
                         }
                         else
                         {
                             int pkt_num = (int)(filelen - head)/DATA_SIZE +((int)(filelen - head)%DATA_SIZE > 0);
                             for(i=0;i<pkt_num-1;i++) //resend the current window
                             {
                                 resend.seq = (int)head + i*DATA_SIZE;
                                 printf("resend seq: %d\n",resend.seq);
                                 m=fread(buf_3,1,DATA_SIZE,fp);
                                 resend.length = m;
                                 memcpy(resend.data,buf_3,DATA_SIZE);
                                 n=sendto(sockfd,&resend,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                                 if(n<0) error("ERROR sending to socket");
                                 memset(resend.data,0,DATA_SIZE);
                                 bzero(buf_3,DATA_SIZE);
                             }
                             resend.seq = (int)head + i*DATA_SIZE;
                             resend.flg = 1;
                             printf("resend seq: %d\n",resend.seq);
                             long cur = ftell(fp);
                             long read_len = filelen - cur;
                             m=fread(buf_3,1,read_len,fp);
                             resend.length = m;
                             memcpy(resend.data,buf_3,read_len);
                             n=sendto(sockfd,&resend,HEAD_LEN+read_len,0,(struct sockaddr*)&cli_addr,clilen);
                             if(n<0) error("ERROR sending to socket");
                             memset(resend.data,0,DATA_SIZE);
                             bzero(buf_3,DATA_SIZE);
                         }
                         free(buf_3);
                     }/*end of timeout*/
                 }/*end of while(1) recv acks*/
                
                 /*send finish signal to client*/
                /* packet finsignal;
                 finsignal.seq = (int)filelen;
                 finsignal.ack = 0;
                 finsignal.flg = 1;
                 finsignal.rwnd = 0;
                 n=sendto(sockfd,&finsignal,HEAD_LEN,0,(struct sockaddr*)&cli_addr,clilen);
                 if(n<0) error("ERROR sending to socket");
                 */
                 /*
                 while(1) //wait for finish ack
                 {
                     FD_SET(sockfd, &rd_fds);
                     finsock = select(sockfd+1, &rd_fds, NULL, NULL, &tv);
                     if(finsock==-1)
                     {
                         error("Select error!\n");
                     }
                     if(finsock)
                     {
                        n = recvfrom(sockfd,rbuffer,HEAD_LEN-1,0,(struct sockaddr*)&cli_addr,&clilen);
                        if (n < 0) error("ERROR receiving from socket");
                        recvpkt = (packet*)rbuffer;
                        //bzero(rbuffer,HEAD_LEN);
                        if(recvpkt->flg==5)
                        {
                            printf("file transfer completed\n");
                            break;
                        }
                     }//end of if(finsock)
                    else // timeout resend finish signal
                     {
                         n = sendto(sockfd,&finsignal,HEAD_LEN,0,(struct sockaddr*)&cli_addr,clilen);
                         if(n<0) error("ERROR sending to socket");
                     }
                 } // end of waiting for fin ack
             */
             }/*end of if(fp) */
         }/* end of if(sgn) */
     } /* end of while */
     return 0; /* we never get here */
}

/******** DOSTUFF() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/
    
    /*while(sgn)
    {
        switch (flg) {
            case 0: //ack=0; syn=0; fin=0
                break;
            case 1: //ack=0; syn=0; fin=1 close request
                break;
            case 2: //ack=0; syn=1;fin=0 connection set up request
                IniSeq = seq; //获取seq初值
                // 返回flg=6; ack=seq+recvdata
                break;
            case 3: //ack=0; syn=1; fin=1;
                break;
            case 4: //ack=1; syn=0; fin=0 normal ack packet
                if(ack==LastSeq+1) //如果ack正常，即ack值等于下一个包的seqnum
                {
                   //cwnd向后滑动一个MSS
                }
                break;
            case 5: //ack=1; syn=0; fin=1
                break;
            case 6: //ack=1; syn=1; fin=0
                break;
            case 7: //ack=1; syn=1; fin=1
                break;
            default:
                break;
        }
    }
    
        m = sendto(sock,sbuffer,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
    if (m < 0) error("ERROR sending to socket");*/
int CheckHdr(packet* recvpkt)
{
    if(recvpkt->flg == 2)
    {
     
        return 0;
    }
    else
    return 1;
}
int CheckAck(packet* recvpkt,long head, int cwnd)
{
    long min_ack;
    long max_ack;
    min_ack = head + DATA_SIZE;
    max_ack = head + cwnd*DATA_SIZE;
    if(recvpkt->ack == min_ack)
    {
        return 1;
    }
    else if((recvpkt->ack <= max_ack) && (recvpkt->ack > min_ack))
    {
        return 2;
    }
    else
        return 0;
}
