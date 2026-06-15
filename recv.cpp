#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include "msg.h"    /* For the message struct */

using namespace std;

/* The size of the shared memory segment */
#define SHARED_MEMORY_CHUNK_SIZE 1000

/* The ids for the shared memory segment and the message queue */
int shmid, msqid;

/* The pointer to the shared memory */
void *sharedMemPtr = NULL;


/**
 * The function for receiving the name of the file
 * @return - the name of the file received from the sender
 */
string recvFileName()
{
    /* The file name received from the sender */
    string fileName;
        
    /* TODO: declare an instance of the fileNameMsg struct to be
     * used for holding the message received from the sender.
     */
    fileNameMsg fileNMSG;

    /* TODO: Receive the file name using msgrcv() */
    // msqid is a global variable initialized in your main/init function
    if (msgrcv(msqid, &fileNMSG, sizeof(fileNameMsg) - sizeof(long), FILE_NAME_TRANSFER_TYPE, 0) < 0)
    {
        perror("msgrcv failed");
        exit(-1);
    }
    
    // Assign the char array from the message buffer to our C++ string
    fileName = fileNMSG.fileName;
    
    /* TODO: return the received file name */
    return fileName;
}
 /**
 * Sets up the shared memory segment and message queue
 * @param shmid - the id of the allocated shared memory 
 * @param msqid - the id of the shared memory
 * @param sharedMemPtr - the pointer to the shared memory
 */
void init(int& shmid, int& msqid, void*& sharedMemPtr)
{
	
	/* 1. Create a file called keyfile.txt containing string "Hello world" using C-strings */
    FILE* keyFile = fopen("keyfile.txt", "w");
    if (keyFile != NULL) {
        fprintf(keyFile, "Hello world");
        fclose(keyFile);
    } else {
        perror("Failed to create keyfile.txt");
        exit(-1);
    }

    /* 2. Use ftok("keyfile.txt", 'a') in order to generate the key. */
    key_t key = ftok("keyfile.txt", 'a');
    if (key == -1) {
        perror("ftok failed");
        exit(-1);
    }

    /* 3. Allocate a shared memory segment. 
          0666 | IPC_CREAT creates it with read/write permissions if it doesn't exist. */
    shmid = shmget(key, SHARED_MEMORY_CHUNK_SIZE, 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget failed");
        exit(-1);
    }

    /* 4. Attach to the shared memory */
    sharedMemPtr = shmat(shmid, NULL, 0);
    if (sharedMemPtr == (void*)-1) {
        perror("shmat failed");
        exit(-1);
    }

    /* 5. Create a message queue */
    msqid = msgget(key, 0666 | IPC_CREAT);
    if (msqid == -1) {
        perror("msgget failed");
        exit(-1);
    }
}
 

/**
 * The main loop
 * @param fileName - the name of the file received from the sender.
 * @return - the number of bytes received
 */
unsigned long mainLoop(const char* fileName)
{
	//* The size of the message received from the sender */
    int msgSize = -1;
    
    /* The number of bytes received */
    int numBytesRecv = 0;
    
    /* The string representing the file name received from the sender */
    string recvFileNameStr = fileName;
    
    /* TODO: append __recv to the end of file name */
    recvFileNameStr.append("__recv");
    
    /* Open the file for writing */
    FILE* fp = fopen(recvFileNameStr.c_str(), "w");
            
    /* Error checks */
    if(!fp)
    {
        perror("fopen");    
        exit(-1);
    }
        
    /* Keep receiving until the sender sets the size to 0, indicating that
     * there is no more data to send.
     */ 
    while(msgSize != 0)
    {   
        /* TODO: Receive the message and get the value of the size field. The message will be of 
         * of type SENDER_DATA_TYPE. That is, a message that is an instance of the message struct with 
         * mtype field set to SENDER_DATA_TYPE (the macro SENDER_DATA_TYPE is defined in 
         * msg.h).  If the size field of the message is not 0, then we copy that many bytes from 
         * the shared memory segment to the file. Otherwise, if 0, then we close the file 
         * and exit.
         */
        message rcvMsg;
        if (msgrcv(msqid, &rcvMsg, sizeof(message) - sizeof(long), SENDER_DATA_TYPE, 0) < 0)
        {
            perror("msgrcv (SENDER_DATA_TYPE) failed");
            fclose(fp);
            exit(-1);
        }
        
        // Update msgSize with the value read from the message queue
        msgSize = rcvMsg.size;
        
        /* If the sender is not telling us that we are done, then get to work */
        if(msgSize != 0)
        {
            /* TODO: count the number of bytes received */
            numBytesRecv += msgSize;
            
            /* Save the shared memory to file */
            if(fwrite(sharedMemPtr, sizeof(char), msgSize, fp) < static_cast<size_t>(msgSize))
            {
                perror("fwrite");
            }
            
            /* TODO: Tell the sender that we are ready for the next set of bytes. 
             * I.e., send a message of type RECV_DONE_TYPE. That is, a message
             * of type ackMessage with mtype field set to RECV_DONE_TYPE. 
             */
            ackMessage ackMsg;
            ackMsg.mtype = RECV_DONE_TYPE;
            
            if (msgsnd(msqid, &ackMsg, sizeof(ackMessage) - sizeof(long), 0) < 0)
            {
                perror("msgsnd (RECV_DONE_TYPE) failed");
                fclose(fp);
                exit(-1);
            }
        }
        /* We are done */
        else
        {
            /* Close the file */
            fclose(fp);
        }
    }
    
    return numBytesRecv;
}



/**
 * Performs cleanup functions
 * @param sharedMemPtr - the pointer to the shared memory
 * @param shmid - the id of the shared memory segment
 * @param msqid - the id of the message queue
 */
void cleanUp(const int& shmid, const int& msqid, void* sharedMemPtr)
{
	/* TODO: Detach from shared memory */
    if (sharedMemPtr != nullptr && sharedMemPtr != (void*)-1) {
        if (shmdt(sharedMemPtr) == -1) {
            perror("shmdt failed");
        }
    }
    
    /* TODO: Deallocate the shared memory segment */
    // IPC_RMID marks the segment to be destroyed.
    if (shmctl(shmid, IPC_RMID, nullptr) == -1) {
        perror("shmctl IPC_RMID failed");
    }
    
    /* TODO: Deallocate the message queue */
    // This immediately destroys the queue and wakes up any blocked processes.
    if (msgctl(msqid, IPC_RMID, nullptr) == -1) {
        perror("msgctl IPC_RMID failed");
    }
}

/**
 * Handles the exit signal
 * @param signal - the signal type
 */
void ctrlCSignal(int signal)
{
	/* Free system V resources */
	cleanUp(shmid, msqid, sharedMemPtr);
}

int main(int argc, char** argv)
{
	/* TODO: Install a signal handler (see signaldemo.cpp sample file).
     * If user presses Ctrl-c, your program should delete the message
     * queue and the shared memory segment before exiting. You may add 
     * the cleaning functionality in ctrlCSignal().
     */
    // Register ctrlCSignal to intercept SIGINT (Ctrl+C interrupts)
    if (signal(SIGINT, ctrlCSignal) == SIG_ERR) {
        perror("Failed to register SIGINT signal handler");
        exit(-1);
    }
                
    /* Initialize */
    init(shmid, msqid, sharedMemPtr);
    
    /* Receive the file name from the sender */
    string fileName = recvFileName();
    
    /* Go to the main loop */
    fprintf(stderr, "The number of bytes received is: %lu\n", mainLoop(fileName.c_str()));

    /* TODO: Detach from shared memory segment, and deallocate shared memory 
     * and message queue (i.e. call cleanup) 
     */
    cleanUp(shmid, msqid, sharedMemPtr);
        
    return 0;
}