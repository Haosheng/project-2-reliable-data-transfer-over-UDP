
#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h>	/* for the waitpid() system call */
#include <signal.h>	/* signal name macros, and the kill() prototype */
#include <string.h>
#include <time.h>

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
int CheckAck(packet*,long,int,int);
int cal_loss(float pl);
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
     int sign = 0;
     float pl = 0.2;
     socklen_t clilen;
     struct sockaddr_in serv_addr, cli_addr;
     struct sigaction sa;          // for signal SIGCHLD
     srand((unsigned)time(NULL));//random seed

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
         long m;
         packet* recvpkt;
         FILE* fp = NULL;
         long head,tail;
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
         printf("requesting for file: %s\n",startbuf+16);
         recvpkt = (packet*)startbuf;
         sgn = CheckHdr(recvpkt);
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
                 
                 char *buf =(char*)malloc((DATA_SIZE)*sizeof(char));
                 if(buf==NULL)
                 {
                     error("malloc buf fail\n");
                     return(-1);
                 }
                 fseek(fp,0L,SEEK_END);
                 long filelen = ftell(fp);
                 fseek(fp,0L,SEEK_SET);
                 int remainder = cwnd % DATA_SIZE;
                 int num_of_pkt_in_wnd = cwnd/DATA_SIZE + (cwnd % DATA_SIZE > 0);
                 
                 packet firstsend; //packets in the initial window
                 firstsend.ack = 0;
                 firstsend.flg = 0;
                 firstsend.rwnd = 0;
                 if(DATA_SIZE<(int)filelen)
                 {
                     if(cwnd<filelen)
                  {
                     if(remainder ==0)
                     {
                       for(i=0;i<num_of_pkt_in_wnd;i++) //send the initial window
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
                   free(buf);
                   tail = ftell(fp);
                   head = 0;
                    }
                      if(remainder > 0)
                      {
                          for(i=0;i<num_of_pkt_in_wnd-1;i++) //send the initial window
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
                          printf("send seq: %d\n",firstsend.seq);
                          m=fread(buf,1,remainder,fp);
                          firstsend.length = m;
                          memcpy(firstsend.data,buf,remainder);
                          //printf("send data: %s\n",firstsend.data);
                          n=sendto(sockfd,&firstsend,HEAD_LEN+remainder,0,(struct sockaddr*)&cli_addr,clilen);
                          if(n<0) error("ERROR sending to socket");
                          free(buf);
                          tail = ftell(fp);
                          head = 0;
                      }
                  } //end of if(cwnd<filelen)
                     else
                     {
                         int inipkt_num = (int)filelen/DATA_SIZE + ((int)filelen % DATA_SIZE > 0);
                         int remain2 = filelen % DATA_SIZE;
                         if(remain2>0)
                         {
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
                         m=fread(buf,1,remain2,fp);
                         firstsend.length = m;
                         memcpy(firstsend.data,buf,remain2);
                         //printf("send data: %s\n",firstsend.data);
                         n=sendto(sockfd,&firstsend,HEAD_LEN+remain2,0,(struct sockaddr*)&cli_addr,clilen);
                         if(n<0) error("ERROR sending to socket");
                         }
                         else if(remain2==0)
                         {
                             for(i=0;i<inipkt_num;i++) //send the initial window
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

                         }
                         free(buf);
                         tail = ftell(fp);
                         head = 0;
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
                         if(!cal_loss(pl))//drop pkt upon loss
                         {
                             printf("ack:%d packet was lost\n",recvpkt->ack);
                             continue;
                         }
                         printf("\nreceived packet with ack num: %d\n",recvpkt->ack);
                       if(recvpkt->ack == filelen && recvpkt->flg ==5)
                       {
                           printf("file transfer completed\n");
                           fclose(fp);
                           break;
                       }
                       if(CheckAck(recvpkt,head, cwnd, remainder)>0) // receive a new ack
                       {
                           long offset = recvpkt->ack - head;
                           //printf("offset: %ld\n",offset);
                           packet newpkt;
                           newpkt.ack = 0;
                           newpkt.flg = 0;
                           newpkt.rwnd = 0;
                           head = head + offset;
                           //fseek(fp_head,offset,SEEK_CUR);
                           if(head+cwnd>=filelen && sign==0) //last packet
                           {
                               //long tlpos = ftell(fp_tail);
                               printf("head:%ld; tail:%ld\n",head,tail);
                               int pos_1 = ((int)tail % cwnd)/DATA_SIZE +1;
                               printf("pos1:%d\n",pos_1);
                               long tosend = filelen - tail;
                               printf("tosend:%ld\n",tosend);
                               char *buf_2=(char*)malloc(DATA_SIZE*sizeof(char));
                               if(buf_2==NULL)
                               {
                                   error("malloc buf_2 fail\n");
                                   return (-1);
                               }
                               while(1)
                               {
                                   printf("tosend:%ld\n",tosend);
                                   newpkt.seq=(int)tail;
                                   if(pos_1==num_of_pkt_in_wnd)
                                   {
                                       
                                       if(tosend>remainder)
                                       {
                                           tosend-=remainder;
                                           m=fread(buf_2,1,remainder,fp);
                                           newpkt.length = m;
                                           printf("send packet with sequence num: %d\n",newpkt.seq);
                                           memcpy(newpkt.data,buf_2,remainder);
                                           n=sendto(sockfd,&newpkt,HEAD_LEN+remainder,0,(struct sockaddr*)&cli_addr,clilen);
                                           if(n<0) error("ERROR sending to socket");
                                           memset(newpkt.data,0,DATA_SIZE);
                                           bzero(buf_2,DATA_SIZE);
                                           tail+=remainder;
                                       }
                                       else
                                       {
                                           newpkt.flg=1;
                                           m=fread(buf_2,1,tosend,fp);
                                           newpkt.length = m;
                                           memcpy(newpkt.data,buf_2,tosend);
                                           printf("send packet with sequence num: %d\n",newpkt.seq);
                                           //printf("send data: %s\n",newpkt.data);
                                           n=sendto(sockfd,&newpkt,HEAD_LEN+tosend,0,(struct sockaddr*)&cli_addr,clilen);
                                           if(n<0) error("ERROR sending to socket");
                                           sign =1;
                                           break;
                                       }
                                   }
                                   else
                                   {
                                       if(tosend>DATA_SIZE)
                                       {
                                           tosend-=DATA_SIZE;
                                           m=fread(buf_2,1,DATA_SIZE,fp);
                                           newpkt.length = m;
                                           memcpy(newpkt.data,buf_2,DATA_SIZE);
                                           printf("send packet with sequence num: %d\n",newpkt.seq);
                                           //printf("send data: %s\n",newpkt.data);
                                           n=sendto(sockfd,&newpkt,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                                           if(n<0) error("ERROR sending to socket");
                                           memset(newpkt.data,0,DATA_SIZE);
                                           bzero(buf_2,DATA_SIZE);
                                           tail+=DATA_SIZE;
                                       }
                                       else
                                       {
                                           newpkt.flg=1;
                                           m=fread(buf_2,1,tosend,fp);
                                           newpkt.length = m;
                                           memcpy(newpkt.data,buf_2,tosend);
                                           printf("send packet with sequence num: %d\n",newpkt.seq);
                                           //printf("send data: %s\n",newpkt.data);
                                           n=sendto(sockfd,&newpkt,HEAD_LEN+tosend,0,(struct sockaddr*)&cli_addr,clilen);
                                           if(n<0) error("ERROR sending to socket");
                                           sign = 1;
                                           break;
                                       }
                                   }
                                   pos_1++;
                               }
                               
                               free(buf_2);
                         }
                           else if(head+cwnd<filelen)
                            {
                                int newpktnum = ((int)offset)/DATA_SIZE + (((int)offset) % DATA_SIZE>0);
                                int remain3 = (int)offset % DATA_SIZE;
                                char* buf_1=(char*)malloc((DATA_SIZE)*sizeof(char));
                                if(buf_1==NULL)
                                {
                                    error("malloc buf_1 fail\n");
                                    return (-1);
                                }
                                if(remain3==0)
                                {
                                int j=0;
                                for(j=0;j<newpktnum;j++)
                                {
                                    m=fread(buf_1,1,DATA_SIZE,fp);
                                    newpkt.length = m;
                                    newpkt.seq = (int)tail + j*DATA_SIZE;
                                    printf("send packet with sequence num: %d\n",newpkt.seq);
                                    memcpy(newpkt.data,buf_1,DATA_SIZE);
                                    //printf("send data: %s\n",newpkt.data);
                                    n=sendto(sockfd,&newpkt,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                                    if(n<0) error("ERROR sending to socket");
                                    memset(newpkt.data,0,DATA_SIZE);
                                    bzero(buf_1,DATA_SIZE);
                                }
                               
                                tail = tail + offset;
                                //printf("tail:%ld\n",tail);
                                free(buf_1);
                               }
                                else if(remain3>0)
                                {
                                    int pos = ((int)tail % cwnd)/DATA_SIZE +1;
                                    int j=0,flag=0;
                                    for(j=0;j<newpktnum;j++)
                                    {
                                        if(pos == num_of_pkt_in_wnd)
                                        {
                                            m=fread(buf_1,1,remain3,fp);
                                            newpkt.length = m;
                                            newpkt.seq = (int)tail + j*DATA_SIZE;
                                            printf("send packet with sequence num: %d\n",newpkt.seq);
                                            memcpy(newpkt.data,buf_1,remain3);
                                            //printf("send data: %s\n",newpkt.data);
                                            n=sendto(sockfd,&newpkt,HEAD_LEN+remain3,0,(struct sockaddr*)&cli_addr,clilen);
                                            if(n<0) error("ERROR sending to socket");
                                            memset(newpkt.data,0,DATA_SIZE);
                                            bzero(buf_1,DATA_SIZE);
                                            flag =1;
                                        }
                                        else
                                        {
                                            m=fread(buf_1,1,DATA_SIZE,fp);
                                            newpkt.length = m;
                                            if(flag ==0)
                                                newpkt.seq = (int)tail + j*DATA_SIZE;
                                            else if(flag==1)
                                                newpkt.seq =(int)tail + (j-1)*DATA_SIZE + remain3;
                                            printf("send packet with sequence num: %d\n",newpkt.seq);
                                            memcpy(newpkt.data,buf_1,DATA_SIZE);
                                            //printf("send data: %s\n",newpkt.data);
                                            n=sendto(sockfd,&newpkt,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                                            if(n<0) error("ERROR sending to socket");
                                            memset(newpkt.data,0,DATA_SIZE);
                                            bzero(buf_1,DATA_SIZE);
                                        }
                                        pos++;
                                        
                                    }
                                    free(buf_1);
                                    tail = tail + offset;
                                }
                                    
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
                         if((head+cwnd) <= filelen)
                         {
                            if(remainder==0)
                            {
                             for(i=0;i<num_of_pkt_in_wnd;i++) //resend the current window
                            {
                             resend.seq = (int)head + i*DATA_SIZE;
                             printf("resend packet with sequence num: %d\n",resend.seq);
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
                             else if(remainder>0)
                             {
                                 int pos_2 = ((int)head % cwnd)/DATA_SIZE +1;
                                 int j=0,flag=0;
                                 for(j=0;j<num_of_pkt_in_wnd;j++)
                                 {
                                     if(pos_2 == num_of_pkt_in_wnd)
                                     {
                                         m=fread(buf_3,1,remainder,fp);
                                         resend.length = m;
                                         resend.seq = (int)head + j*DATA_SIZE;
                                         printf("resend packet with sequence num: %d\n",resend.seq);
                                         memcpy(resend.data,buf_3,remainder);
                                         //printf("send data: %s\n",newpkt.data);
                                         n=sendto(sockfd,&resend,HEAD_LEN+remainder,0,(struct sockaddr*)&cli_addr,clilen);
                                         if(n<0) error("ERROR sending to socket");
                                         memset(resend.data,0,DATA_SIZE);
                                         bzero(buf_3,DATA_SIZE);
                                         flag =1;
                                     }
                                     else
                                     {
                                         m=fread(buf_3,1,DATA_SIZE,fp);
                                         resend.length = m;
                                         if(flag ==0)
                                             resend.seq = (int)head + j*DATA_SIZE;
                                         else if(flag==1)
                                             resend.seq =(int)head + (j-1)*DATA_SIZE + remainder;
                                         printf("resend packet with sequence num: %d\n",resend.seq);
                                         memcpy(resend.data,buf_3,DATA_SIZE);
                                         //printf("send data: %s\n",newpkt.data);
                                         n=sendto(sockfd,&resend,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                                         if(n<0) error("ERROR sending to socket");
                                         memset(resend.data,0,DATA_SIZE);
                                         bzero(buf_3,DATA_SIZE);
                                     }
                                     pos_2++;
                                     
                                 }
                                 free(buf_3);
                             }
                         }
                         else
                         {
                             int pos_3 = ((int)head % cwnd)/DATA_SIZE +1;
                             printf("pos_3:%d\n",pos_3);
                             long tosend_1 = filelen - head;
                             char *buf_3=(char*)malloc(DATA_SIZE*sizeof(char));
                             if(buf_3==NULL)
                             {
                                 error("malloc buf_3 fail\n");
                                 return (-1);
                             }
                             printf("head:%ld\n",head);
                             resend.seq=(int)head;
                             while(1)
                             {
                                 printf("tosend_1:%ld\n",tosend_1);
                                 
                                 if(pos_3==num_of_pkt_in_wnd)
                                 {
                                     if(tosend_1>remainder)
                                     {
                                         tosend_1-=remainder;
                                         m=fread(buf_3,1,remainder,fp);
                                         resend.length = m;
                                         printf("resend packet with sequence num: %d\n",resend.seq);
                                         memcpy(resend.data,buf_3,remainder);
                                         n=sendto(sockfd,&resend,HEAD_LEN+remainder,0,(struct sockaddr*)&cli_addr,clilen);
                                         if(n<0) error("ERROR sending to socket");
                                         memset(resend.data,0,DATA_SIZE);
                                         bzero(buf_3,DATA_SIZE);
                                         resend.seq+=remainder;
                                     }
                                     else
                                     {
                                         resend.flg=1;
                                         m=fread(buf_3,1,tosend_1,fp);
                                         resend.length = m;
                                         memcpy(resend.data,buf_3,tosend_1);
                                         printf("resend packet with sequence num: %d\n",resend.seq);
                                         //printf("send data: %s\n",newpkt.data);
                                         n=sendto(sockfd,&resend,HEAD_LEN+tosend_1,0,(struct sockaddr*)&cli_addr,clilen);
                                         if(n<0) error("ERROR sending to socket");
                                         break;
                                     }
                                 }
                                 else
                                 {
                                     if(tosend_1>DATA_SIZE)
                                     {
                                         tosend_1-=DATA_SIZE;
                                         m=fread(buf_3,1,DATA_SIZE,fp);
                                         resend.length = m;
                                         memcpy(resend.data,buf_3,DATA_SIZE);
                                         //printf("send data: %s\n",newpkt.data);
                                         printf("resend packet with sequence num: %d\n",resend.seq);
                                         n=sendto(sockfd,&resend,PKT_SIZE,0,(struct sockaddr*)&cli_addr,clilen);
                                         if(n<0) error("ERROR sending to socket");
                                         memset(resend.data,0,DATA_SIZE);
                                         bzero(buf_3,DATA_SIZE);
                                         resend.seq+=DATA_SIZE;
                                     }
                                     else
                                     {
                                         resend.flg=1;
                                         m=fread(buf_3,1,tosend_1,fp);
                                         resend.length = m;
                                         memcpy(resend.data,buf_3,tosend_1);
                                         //printf("send data: %s\n",r.data);
                                         printf("resend packet with sequence num: %d\n",resend.seq);
                                         n=sendto(sockfd,&resend,HEAD_LEN+tosend_1,0,(struct sockaddr*)&cli_addr,clilen);
                                         if(n<0) error("ERROR sending to socket");
                                         break;
                                     }
                                 }
                                 pos_3++;
                             }
                             
                             free(buf_3);
                     }
                     tv.tv_sec = 0;
                     tv.tv_usec = timeout;/*end of timeout*/
                 }
                 tv.tv_sec = 0;
                 tv.tv_usec = timeout;/*end of while(1) recv acks*/
             }
         }
     }
    return 0; /* we never get here */
}
}
/******** DOSTUFF() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/
    
int CheckHdr(packet* recvpkt)
{
    if(recvpkt->flg == 2)
    {
     
        return 0;
    }
    else
    return 1;
}

int CheckAck(packet* recvpkt,long head, int cwnd, int remainder)
{
    long min_ack;
    long max_ack;
    if(remainder==0)
    {
        min_ack = head+DATA_SIZE;
    }
    else
    {
        min_ack = head + remainder;
    }
    max_ack = head + cwnd;
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

int cal_loss(float pl)
{
    float randpl;
    randpl=((float)rand())/(float)(RAND_MAX/1.0);
    if(randpl<=pl)
        return 0;
    else return 1;
}
