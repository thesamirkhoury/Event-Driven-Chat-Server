
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include "chatServer.h"

#define FALSE 0
#define TRUE 1
static int end_server = 0;

/**
 * signal handler that stops the do-while loop.
 * @param SIG_INT
 */
void intHandler(int SIG_INT) {
    /* use a flag to end_server to break the main loop */
    end_server=TRUE;
}

int main (int argc, char *argv[]){
    //checks if recieved wrong parameter.
    if(argc!=2){
        printf("Usage: chatServer <port>");
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, intHandler);
    int port= atoi(argv[1]);
    conn_pool_t* pool = malloc(sizeof(conn_pool_t));
    init_pool(pool);

    /*************************************************************/
    /* Create an AF_INET stream socket to receive incoming      */
    /* connections on                                            */
    /*************************************************************/
    int fd; /* socket descriptor */
    if((fd=socket(PF_INET,SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    /*************************************************************/
    /* Set socket to be nonblocking. All of the sockets for      */
    /* the incoming connections will also be nonblocking since   */
    /* they will inherit that state from the listening socket.   */
    /*************************************************************/
    int on=1;
    ioctl(fd,(int)FIONBIO,(char*)&on);
    /*************************************************************/
    /* Bind the socket                                           */
    /*************************************************************/
    struct sockaddr_in srv;/* used by bind() */
    /* create the socket */
    srv.sin_family=AF_INET; /* use the Internet addr family */
    srv.sin_port=htons(port);
    /* bind: a client may connect to any of my addresses */
    srv.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(fd, (struct sockaddr*) &srv,sizeof(srv)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    /*************************************************************/
    /* Set the listen back log                                   */
    /*************************************************************/
    if(listen(fd,5)<0){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    /*************************************************************/
    /* Initialize fd_sets                                         */
    /*************************************************************/
    FD_SET(fd,&pool->read_set);
    /*************************************************************/
    /* Loop waiting for incoming connects, for incoming data or  */
    /* to write data, on any of the connected sockets.           */
    /*************************************************************/
    do
    {
        //updating the maxfd in the pool in every run.
        conn_t *curr=pool->conn_head;
        while(curr!=NULL){
            if(curr->fd>pool->maxfd){
                pool->maxfd=curr->fd;
            }
            curr=curr->next;
        }
        if(pool->maxfd<fd){
            pool->maxfd=fd;
        }
        /**********************************************************/
        /* Copy the master fd_set over to the working fd_set.     */
        /**********************************************************/
        pool->ready_write_set=pool->write_set;
        pool->ready_read_set=pool->read_set;
        /**********************************************************/
        /* Call select()                                */
        /**********************************************************/
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        pool->nready=select(pool->maxfd+1,&pool->ready_read_set,&pool->ready_write_set,NULL,NULL);
        if(pool->nready<0){
            break;
        }
        /**********************************************************/
        /* One or more descriptors are readable or writable.      */
        /* Need to determine which ones they are.                 */
        /**********************************************************/
        int i;
        for (i=0;i<pool->maxfd+1&&pool->nready>0;i++)
        {
            /* Each time a ready descriptor is found, one less has  */
            /* to be looked for.  This is being done so that we     */
            /* can stop looking at the working set once we have     */
            /* found all of the descriptors that were ready         */

            /*******************************************************/
            /* Check to see if this descriptor is ready for read   */
            /*******************************************************/
            if (FD_ISSET(i,&pool->ready_read_set))
            {
                /***************************************************/
                /* A descriptor was found that was readable          */
                /* if this is the listening socket, accept one      */
                /* incoming connection that is queued up on the     */
                /*  listening socket before we loop back and call   */
                /* select again.                              */
                /****************************************************/
                char buffer[BUFFER_SIZE]="";
                if(i==fd){
                    struct sockaddr_in cli;/* returned by accept()*/
                    int cli_len=sizeof(cli);/* used by accept() */
                    int newfd;
                    newfd=accept(i,(struct sockaddr*)&cli,&cli_len);
                    printf("New incoming connection on sd %d\n", newfd);
                    pool->nready--;
                    add_conn(newfd,pool);
                }
                    /****************************************************/
                    /* If this is not the listening socket, an           */
                    /* existing connection must be readable             */
                    /* Receive incoming data his socket             */
                    /****************************************************/

                else {
                    printf("Descriptor %d is readable\n", i);
                    int chk=(int) read(i, buffer, BUFFER_SIZE);
                    printf("%d bytes received from sd %d\n", (int)strlen(buffer), i);
                    pool->nready--;
                    /* If the connection has been closed by client        */
                    /* remove the connection (remove_conn(...))           */
                    if (chk==0){
                        printf("Connection closed for sd %d\n",i);
                        remove_conn(i,pool);
                    }
                    /**********************************************/
                    /* Data was received, add msg to all other    */
                    /* connectios                         */
                    /**********************************************/
                    add_msg(i, buffer, (int)strlen(buffer), pool);
                }
            } /* End of if (FD_ISSET()) */
            /*******************************************************/
            /* Check to see if this descriptor is ready for write  */
            /*******************************************************/
            if (FD_ISSET(i,&pool->ready_write_set)){
                /* try to write all msgs in queue to sd */
                write_to_client(i,pool);
                pool->nready--;
            }
            /*******************************************************/
        } /* End of loop through selectable descriptors */

    } while (end_server == FALSE);

    /*************************************************************/
    /* If we are here, Control-C was typed,                    */
    /* clean up all open connections                        */
    /*************************************************************/

    int i=0;
    int size=(int)pool->nr_conns;//how many active connections are still available.
    int removeCon[pool->nr_conns];// int array that holds all the active connections sd.

    conn_t *curr=pool->conn_head;
    while (curr!=NULL){
        removeCon[i]=curr->fd;
        curr=curr->next;
        i++;
   }
    for(i=size;i>0;i--){
        remove_conn(removeCon[i-1],pool);
    }
    free(pool);
    return 0;
}

/**
 * initiate the connection pool values.
 * @param pool
 * @return 0 if successful, -1 otherwise.
 */
int init_pool(conn_pool_t* pool) {
    pool->maxfd=0;
    pool->nready=0;
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->ready_read_set);
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->ready_write_set);
    pool->conn_head=NULL;
    pool->nr_conns=0;
    return 0;
}
/**
 * adds a new connection to the connection pool, with provided new socket descriptor.
 * @param sd
 * @param pool
 * @return 0 if successful, -1 otherwise.
 */
int add_conn(int sd, conn_pool_t* pool) {
    conn_t * newConnection = malloc(sizeof(conn_t));
    if(newConnection==NULL){
        return -1;
    }
    newConnection->fd=sd;
    newConnection->prev=NULL;
    newConnection->next=NULL;
    newConnection->write_msg_head=NULL;
    newConnection->write_msg_tail=NULL;
    if(pool->conn_head!=NULL){
        newConnection->next=pool->conn_head;
        pool->conn_head->prev=newConnection;
    }
    pool->conn_head=newConnection;
    FD_SET(sd,&pool->read_set);
    pool->nr_conns++;
    return 0;
}
/**
 * removes a connection with a provided socket descriptor from the connection pool.
 * @param sd
 * @param pool
 * @return 0 if successful, -1 otherwise.
 */
int remove_conn(int sd, conn_pool_t* pool) {
    printf("removing connection with sd %d \n", sd);
    conn_t *curr=pool->conn_head;
    conn_t *del=NULL;
    while (curr!=NULL){
        if(curr->fd==sd){
            del=curr;
            break;
        } else
            curr=curr->next;
    }
    if(del==NULL){
        return -1;
    }
    if(del==pool->conn_head){
        pool->conn_head=del->next;
    }
    if(del->next!=NULL){
        del->next->prev=del->prev;
    }
    if(del->prev!=NULL){
        del->prev->next=del->next;
    }
    FD_CLR(sd,&pool->write_set);
    FD_CLR(sd,&pool->read_set);
    pool->nr_conns--;
    close(sd);
    free(del);
    return 0;
}
/**
 * adds a message to the connection struct of every connection in the pool, other than the sender sd.
 * @param sd
 * @param buffer
 * @param len
 * @param pool
 * @return 0 if successful, -1 otherwise.
 */
int add_msg(int sd,char* buffer,int len,conn_pool_t* pool) {
    conn_t *tmp=pool->conn_head;
    while(tmp!=NULL){
        if(tmp->fd!=sd){
            msg_t *newMsg= malloc(sizeof (msg_t));
            if(newMsg==NULL){
                return -1;
            }
            newMsg->message=(char*)(malloc(sizeof (char)*len+1));
            if(newMsg->message==NULL){
                return -1;
            }
            strcpy(newMsg->message,buffer);
            newMsg->message[len+1]='\0';
            newMsg->size=len;
            newMsg->next=NULL;
            newMsg->prev=NULL;

            if(tmp->write_msg_head==NULL){
                tmp->write_msg_head = newMsg;
            }
            else {
                msg_t *tmpMSG=tmp->write_msg_head;
                while(tmpMSG->next != NULL){
                    tmpMSG=tmpMSG->next;
                }
                tmpMSG->next=newMsg;
                newMsg->prev=tmp->write_msg_tail;
                newMsg->next=NULL;
            }
            tmp->write_msg_tail = newMsg;
            FD_SET(tmp->fd,&pool->write_set);
        }
        tmp=tmp->next;
    }

    return 0;
}
/**
 * writes all the messages in the queue to the client.
 * @param sd
 * @param pool
 * @return 0 if successful, -1 otherwise.
 */
int write_to_client(int sd,conn_pool_t* pool) {
    conn_t *tmp=pool->conn_head;
    while (tmp!=NULL){
        if(tmp->fd==sd){
            msg_t *msg=tmp->write_msg_head;
            msg_t *freeMe;
            while (msg!=NULL){
                int chkWrite=(int)write(sd,msg->message,msg->size);
                if(chkWrite==-1){
                    return -1;
                }
                freeMe=msg;
                msg=msg->next;
                free(freeMe->message);
                free(freeMe);
            }
            FD_CLR(tmp->fd,&pool->write_set);
            tmp->write_msg_head=NULL;
            tmp->write_msg_tail=NULL;
        }
        tmp=tmp->next;
    }
    return 0;
}


