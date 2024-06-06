/*
 * auctionclient
 * CSSE2310 A4
 * 47435278 - Hamza Khurram
 */

#include <csse2310a4.h>
#include <csse2310a3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#define NUM_OF_ARGS 2

#define AUCTION_PROGRESS_MSG "Auction in progress - unable to exit yet\n"

// Valid output from auctioneer.
#define LISTED ":listed"
#define UNSOLD ":unsold"
#define SOLD ":sold"
#define BID ":bid"
#define OUTBID ":outbid"
#define WON ":won"

// Input from stdin to compare to.
#define QUIT "quit"
#define COMMENT '#'

// Error messages
#define USAGE_ERR_MSG "Usage: auctionclient portno\n"
#define CONNECT_ERR_MSG "auctionclient: unable to connect to port %s\n"
#define PIPE_ERR_MSG "auctionclient: server connection terminated\n"
#define AUCTION_EXIT_MSG "Exiting with auction still in progress\n"

// Exit statuses for program
enum ExitStatus {
    OK = 0,
    USAGE_ERR = 2,
    CONNECT_ERR = 4,
    PIPE_ERR = 5,
    AUCTION_EXIT = 14
};

// Program parameters used across the two threads
typedef struct {
    struct addrinfo* ai;
    int inputFd;
    FILE* input;
    int outputFd;
    FILE* output;
    int numOfListed;
    int numOfBids;
} ProgramParameters;

// Function prototypes
void check_args(int argc, char** argv);
int connect_port(const char* port, struct addrinfo* ai,
	struct addrinfo hints); 
void pipe_error(int s);
void* read_input(void* params);
void get_auctioneer_output(ProgramParameters* parameters);
void free_memory(ProgramParameters* parameters);

/* pipe_error()
 * -----------
 * Signal handler for when SIGPIPE has been detected.
 *
 * s: an number containg information about the signal.
 *
 * Returns: void
 */
void pipe_error(int s) {
    fprintf(stderr, PIPE_ERR_MSG);
    exit(PIPE_ERR);
}

int main(int argc, char** argv) {
    check_args(argc, argv);
    const char* port = argv[1];
    
    // Initialise struct for client
    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));

    // Initialise signal handler
    struct sigaction pipeInterrupt;
    memset(&pipeInterrupt, 0, sizeof(pipeInterrupt));
    pipeInterrupt.sa_handler = pipe_error;
    sigaction(SIGPIPE, &pipeInterrupt, 0);

    int fd = connect_port(port, ai, hints);

    // Initialise struct with program parameters
    ProgramParameters* parameters = malloc(sizeof(ProgramParameters));
    parameters->inputFd = fd;
    parameters->outputFd = dup(fd);
    parameters->numOfListed = 0;
    parameters->numOfBids = 0;

    // Create a thread to read stdin and send to auctioneer
    pthread_t tid;
    pthread_create(&tid, 0, read_input, parameters);

    // Read output from auctioneer
    get_auctioneer_output(parameters);

    pthread_exit(NULL);
    return 0;
}

/* get_auctioneer_output()
 * --------------
 * Reads output from auctioneer and interprets it.
 *
 * parameters: a struct containing the file descriptors,number of items
 * 	listed, and number of items bidded on.
 * splitOutput: an array with the output from auctioneer split by ' '.
 *
 * Returns: void
 */
void get_auctioneer_output(ProgramParameters* parameters) {
    FILE* input = fdopen(parameters->inputFd, "r");
    char* outputLine;
    while ((outputLine = read_line(input))) {
	// Send output from auctioneer to stdout
	printf("%s\n", outputLine);
	fflush(stdout);

	char** splitOutput = split_by_char(outputLine, ' ', 0);
	
	// Check if user has put something for selling.
	if (strcmp(splitOutput[0], LISTED) == 0) {
	    ++(parameters->numOfListed);
	}

	// Check if the listed item is sold or unsold
	if (strcmp(splitOutput[0], UNSOLD) == 0 ||
		strcmp(splitOutput[0], SOLD) == 0) {
	    --(parameters->numOfListed);
	}

	// Check if user has bid on something
	if (strcmp(splitOutput[0], BID) == 0) {
	    parameters->numOfBids++;
	}

	// Check if user has been outbid on item
	if (strcmp(splitOutput[0], OUTBID) == 0 ||
		strcmp(splitOutput[0], WON) == 0) {
	    parameters->numOfBids--;
	}
    }
    
    if (outputLine == NULL) {
	fprintf(stderr, PIPE_ERR_MSG);
	exit(PIPE_ERR);
    }
}

/* check_args()
 * ------------
 * Checks if the correct number of command line arguments are given.
 *
 * argc: the number of command line arguments.
 * argv: an array of arrays of the command line arguments.
 *
 * Errors: Exits with status 2 and usage error message if 2 arguments have
 * 	not been given.
 */
void check_args(int argc, char** argv) {
    if (argc != NUM_OF_ARGS) {
	fprintf(stderr, USAGE_ERR_MSG);
	exit(USAGE_ERR);
    }
}

/* connect_port()
 * --------------
 * Creates a socket to connect to the auctioneer server.
 *
 * port: the port specified in the command line arguments.
 * ai: a struct which helps connect to the socket.
 * hints: a struct which helps use TCP for the socket.
 *
 * Returns: the file descriptor to the created socket.
 * Errors: Exits with status 4 and connect error message if the specified
 * 	cannot be connected to, or if the socket cannot be created.
 */
int connect_port(const char* port, struct addrinfo* ai,
	struct addrinfo hints) {
    // Use IPv4
    hints.ai_family = AF_INET;
    
    // Use TCP
    hints.ai_socktype = SOCK_STREAM;

    // Check if address is valid. 
    int err;
    if ((err = getaddrinfo("localhost", port, &hints, &ai))) {
	freeaddrinfo(ai);
	fprintf(stderr, CONNECT_ERR_MSG, port);
	exit(CONNECT_ERR);
    }

    // Create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, ai->ai_addr, sizeof(struct sockaddr))) {
	fprintf(stderr, CONNECT_ERR_MSG, port);
	exit(CONNECT_ERR);
    }

    return fd;
}

/* read_input()
 * ------------
 * Gets input from stdin and outputs it to the socket file descriptor.
 *
 * params: a struct containing the file descriptors, number of items listed,
 * 	and number of items bidded on.
 *
 * Returns: Null, however it exits with status 0 if user types quit or EOF.
 * Erros: Exits with status 14 and auction in progress message if user tries
 * 	to quit with items still listed or bidded on.
 */
void* read_input(void* params) {
    ProgramParameters* parameters = (ProgramParameters*) params;
    int outputFd = parameters->outputFd;
    FILE* output = fdopen(outputFd, "w");
    char* line;
    while ((line = read_line(stdin))) {
	// Ignore line that starts with '#' or empty line
	if (line[0] == COMMENT || strlen(line) == 0) {
	    free(line);
	    continue;
	}

	if (strcmp(line, QUIT) == 0) {
	    free(line);
	    // Ensure user is not active in any auctions before exiting
	    if (parameters->numOfListed != 0 || 
		    parameters->numOfBids != 0) {
		printf(AUCTION_PROGRESS_MSG);
		fflush(stdout);
		continue;
	    }
	    exit(0);
	}

	fprintf(output, "%s\n", line);
	fflush(output);
    }
    fclose(output);

    if (line == NULL) {
	if (parameters->numOfListed != 0 || parameters->numOfBids != 0) {
	    fprintf(stderr, AUCTION_EXIT_MSG);
	    exit(AUCTION_EXIT);
	}
	exit(OK);
    }
    return NULL;
}

