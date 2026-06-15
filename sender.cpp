#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "msg.h"    /* For the message struct */

/* The size of the shared memory segment */
#define SHARED_MEMORY_CHUNK_SIZE 1000

/* The ids for the shared memory segment and the message queue */
int shmid, msqid;

/* The pointer to the shared memory */
void* sharedMemPtr;

/**
 * Sets up the shared memory segment and message queue
 * @param shmid - the id of the allocated shared memory 
 * @param msqid - the id of the allocated message queue
 */
void init(int& shmid, int& msqid, void*& sharedMemPtr)
{
    /* 1. Create a file called keyfile.txt containing string "Hello world" using pure C functions */
    FILE* keyFile = fopen("keyfile.txt", "w");
    if (keyFile != NULL) {
        fprintf(keyFile, "Hello world");
        fclose(keyFile);
    } else {
        fprintf(stderr, "Failed to create keyfile.txt\n");
        exit(-1);
    }

    /* 2. Use ftok("keyfile.txt", 'a') in order to generate the key. */
    key_t key = ftok("keyfile.txt", 'a');
    if (key == -1) {
        perror("ftok failed");
        exit(-1);
    }

    /* 3. Get the id of the existing shared memory segment. 
          CRITICAL: Removed IPC_CREAT because the sender attaches to what the receiver built. */
    shmid = shmget(key, SHARED_MEMORY_CHUNK_SIZE, 0666);
    if (shmid == -1) {
        perror("shmget failed (Make sure receiver is running first!)");
        exit(-1);
    }

    /* 4. Attach to the shared memory */
    // Passing NULL lets the OS choose the memory address.
    sharedMemPtr = shmat(shmid, NULL, 0);
    if (sharedMemPtr == (void*)-1) {
        perror("shmat failed");
        exit(-1);
    }

    /* 5. Connect to the existing message queue */
    // CRITICAL: Removed IPC_CREAT because the sender connects to the receiver's queue.
    msqid = msgget(key, 0666);
    if (msqid == -1) {
        perror("msgget failed");
        exit(-1);
    }
} // Fixed: Restored the missing closing bracket

/**
 * Performs the cleanup functions
 * @param sharedMemPtr - the pointer to the shared memory
 * @param shmid - the id of the shared memory segment
 * @param msqid - the id of the message queue
 */
void cleanUp(const int& shmid, const int& msqid, void* sharedMemPtr)
{
    /* Detach from shared memory */
    if (sharedMemPtr != nullptr && sharedMemPtr != (void*)-1) {
        if (shmdt(sharedMemPtr) == -1) {
            perror("shmdt failed");
        }
    }

    /* CRITICAL: Removed shmctl and msgctl IPC_RMID. 
       The receiver owns deallocation, the sender only detaches. */
}

/**
 * The main send function
 * @param fileName - the name of the file
 * @return - the number of bytes sent
 */
unsigned long sendFile(const char* fileName)
{
    /* A buffer to store message we will send to the receiver. */
    message sndMsg; 
    
    /* A buffer to store message received from the receiver. */
    ackMessage rcvMsg;
        
    /* The number of bytes sent */
    unsigned long numBytesSent = 0;
    
    /* Open the file */
    FILE* fp = fopen(fileName, "r");

    /* Was the file open? */
    if(!fp)
    {
        perror("fopen");
        exit(-1);
    }
    
    /* Read the whole file */
    while(!feof(fp))
    {
        /* Read at most SHARED_MEMORY_CHUNK_SIZE from the file and store them in shared memory. */
        size_t bytesRead = fread(sharedMemPtr, sizeof(char), SHARED_MEMORY_CHUNK_SIZE, fp);
        
        if (ferror(fp)) {
            perror("fread error");
            fclose(fp);
            exit(-1);
        }

        sndMsg.size = bytesRead;
        numBytesSent += sndMsg.size;
            
        /* Send a message to the receiver telling him that the data is ready */
        sndMsg.mtype = SENDER_DATA_TYPE;
        if (msgsnd(msqid, &sndMsg, sizeof(message) - sizeof(long), 0) == -1)
        {
            perror("msgsnd (SENDER_DATA_TYPE) failed");
            fclose(fp);
            exit(-1);
        }
        
        /* Wait until the receiver sends back an acknowledgement message */
        if (msgrcv(msqid, &rcvMsg, sizeof(ackMessage) - sizeof(long), RECV_DONE_TYPE, 0) == -1)
        {
            perror("msgrcv (RECV_DONE_TYPE) failed");
            fclose(fp);
            exit(-1);
        }
    }
    
    /** Once we are out of the loop, signal EOF with size = 0 */
    sndMsg.mtype = SENDER_DATA_TYPE;
    sndMsg.size = 0;
    if (msgsnd(msqid, &sndMsg, sizeof(message) - sizeof(long), 0) == -1)
    {
        perror("msgsnd (EOF notification) failed");
        fclose(fp);
        exit(-1);
    }
        
    /* Close the file */
    fclose(fp);
    
    return numBytesSent;
}

/**
 * Used to send the name of the file to the receiver
 * @param fileName - the name of the file to send
 */
void sendFileName(const char* fileName)
{
    /* Get the length of the file name */
    int fileNameSize = strlen(fileName);

    /* Make sure the file name does not exceed maximum buffer size */
    if (fileNameSize >= MAX_FILE_NAME_SIZE)
    {
        fprintf(stderr, "Error: File name exceeds maximum allowed size (%d chars).\n", MAX_FILE_NAME_SIZE - 1);
        exit(-1);
    }

    /* Create an instance of the struct representing the message */
    fileNameMsg nameMsg;

    /* Set the message type FILE_NAME_TRANSFER_TYPE */
    nameMsg.mtype = FILE_NAME_TRANSFER_TYPE;

    /* Set the file name in the message */
    strncpy(nameMsg.fileName, fileName, MAX_FILE_NAME_SIZE - 1);
    nameMsg.fileName[MAX_FILE_NAME_SIZE - 1] = '\0';

    /* Send the message using msgsnd */
    if (msgsnd(msqid, &nameMsg, sizeof(fileNameMsg) - sizeof(long), 0) == -1)
    {
        perror("msgsnd (FILE_NAME_TRANSFER_TYPE) failed");
        exit(-1);
    }
}

int main(int argc, char** argv)
{
    /* Check the command line arguments */
    if(argc < 2)
    {
        fprintf(stderr, "USAGE: %s <FILE NAME>\n", argv[0]);
        exit(-1);
    }
        
    /* Connect to shared memory and the message queue */
    init(shmid, msqid, sharedMemPtr);
    
    /* Send the name of the file */
    sendFileName(argv[1]);
        
    /* Send the file */
    fprintf(stderr, "The number of bytes sent is %lu\n", sendFile(argv[1]));
    
    /* Cleanup */
    cleanUp(shmid, msqid, sharedMemPtr);
        
    return 0;
}