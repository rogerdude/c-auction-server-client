/*
 * auctioneer
 * CSSE2310 A4
 * Author: Hamza
 */

#include <csse2310a4.h>
#include <csse2310a3.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_ARGS 5
#define NUM_OF_VALID_ARGS 2
#define MAXCONN "--maxconn"
#define LISTENON "--listenon"

#define DEFAULT_PORT "0"
#define MIN_PORT 1024
#define MAX_PORT 65535

// Error messages
#define USAGE_ERR_MSG "Usage: auctioneer [--maxconn num-connections] " \
    "[--listenon portnumber]\n"
#define PORT_CONNECT_ERR_MSG "auctioneer: unable to listen on port\n"

// Exit codes for program
enum ExitCodes {
    USAGE_ERR = 10,
    PORT_CONNECT_ERR = 17
};

typedef struct {
    pthread_t tid;
    int clientFd;
    FILE* input;
    FILE* output;
} Client;

typedef struct {
    Client seller;
    bool sellerActive;
    char* item;
    int reserve;
    int duration;
    bool highestBidder;
    int highestBid;
    double expiryTime;
    Client topBidder;
    bool bidderActive;
} ItemList;

typedef struct {
    sem_t* lock;
    int numConnections;
    const char* portNumber;
    int socketFd;
    int numOfItems;
    ItemList* items;
    int numOfClients;
    int numOfActiveClients;
    Client* clients;
    int numOfExited;
    pthread_t* exitedTids;
} ProgramParameters;

// Function prototypes
void check_argc(int argc); 
void check_valid_args(int argc, char** argv); 
int get_num_connections(int argc, char** argv);
const char* get_port_number(int argc, char** argv);
void init_params(int argc, char** argv, ProgramParameters* parameters);
void init_client(ProgramParameters* parameters, int clientFd);
void init_lock(sem_t* lock);
void take_lock(sem_t* lock);
void release_lock(sem_t* lock);
int create_socket(const char* portNumber); 
void print_port_and_listen(ProgramParameters* parameters);
void* check_time(void* params);
void* auction_client(void* fd);
void check_input(int length, char** splitLine, ProgramParameters* parameters,
	FILE* output, int clientIndex);
void check_sell(char** splitLine, ProgramParameters* parameters,
	Client client);
bool validate_bid_input(char** splitLine, ProgramParameters* parameters,
	Client client);
int find_item(char** splitLine, ProgramParameters* parameters,
	FILE* outputClient);
void list_all_items(ProgramParameters* parameters, FILE* outputClient);
void place_bid(char** splitLine, ProgramParameters* parameters,
	Client client);
void remove_item(ProgramParameters* parameters, int itemIndex);

/* init_lock()
 * -----------
 * Initialises the semaphore lock.
 *
 * lock: a pointer to the semaphore lock variable.
 *
 * Returns: void
 */
void init_lock(sem_t* lock) {
    sem_init(lock, 0, 1);
}

/* take_lock()
 * -----------
 * Locks other threads from accessing shared data struct.
 *
 * lock: a pointer to the semaphore lock variable.
 *
 * Returns: void
 */
void take_lock(sem_t* lock) {
    sem_wait(lock);
}

/* release_lock()
 * -----------
 * Allows other threads to access shared data struct.
 *
 * lock: a pointer to the semaphore lock variable.
 *
 * Returns: void
 */
void release_lock(sem_t* lock) {
    sem_post(lock);
}

int main(int argc, char** argv) {
    check_argc(argc);
    check_valid_args(argc, argv);
    ProgramParameters* parameters = malloc(sizeof(ProgramParameters));
    init_params(argc, argv, parameters);
    print_port_and_listen(parameters);

    sem_t lock;
    init_lock(&lock);
    parameters->lock = &lock;

    // Start thread for checking time expiry.
    pthread_t timeTid;
    pthread_create(&timeTid, NULL, check_time, parameters);

    // Accept connections from clients.
    int clientFd;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;
    while (1) {
	if (parameters->numConnections != -1) {
	    while (1) {
		take_lock(parameters->lock);
		if (parameters->numOfActiveClients 
			< parameters->numConnections) {
		    release_lock(parameters->lock);
		    break;
		}
		release_lock(parameters->lock);
		usleep(100000);
	    }
	}

	fromAddrSize = sizeof(struct sockaddr_in);
	
	// Wait for new connection.
	clientFd = accept(parameters->socketFd, (struct sockaddr*) &fromAddr,
		&fromAddrSize);
	if (clientFd < 0) {
	    fprintf(stderr, PORT_CONNECT_ERR_MSG);
	    exit(PORT_CONNECT_ERR);
	}
	init_client(parameters, clientFd);
    }
    pthread_detach(timeTid);

    return 0;
}

/* init_params()
 * -------------
 * Reads command line for optional arguments, creates a socket, and
 * 	initialises initial variables in parameters struct.
 *
 * argc: the number of command line arguments.
 * argv: an array of arrays containing the command line arguments.
 * parameters: a data struct containing all the data for the program.
 *
 * Returns: void
 */
void init_params(int argc, char** argv, ProgramParameters* parameters) {
    parameters->numConnections = get_num_connections(argc, argv);
    parameters->portNumber = get_port_number(argc, argv);
    parameters->socketFd = create_socket(parameters->portNumber);
    parameters->numOfItems = 0;
    parameters->items = malloc(sizeof(ItemList) * parameters->numOfItems);
    parameters->numOfClients = 0;
    parameters->numOfActiveClients = 0;
    parameters->clients = malloc(sizeof(Client) * parameters->numOfClients);
    parameters->numOfExited = 0;
    parameters->exitedTids = malloc(sizeof(pthread_t) *
	    parameters->numOfExited);
}

/* init_client()
 * ------------
 * Initialises variables in parameters struct for a new client, and starts a
 * 	thread for the new client.
 *
 * parameters: a data struct containing all the data for the program.
 * clientFd: the file descriptor of the client to read and write to.
 *
 * Returns void
 */
void init_client(ProgramParameters* parameters, int clientFd) {
    parameters->clients = realloc(parameters->clients, sizeof(Client)
	    * ++(parameters->numOfClients));
    parameters->clients[parameters->numOfClients - 1].clientFd = clientFd;
    parameters->numOfActiveClients++;
    
    pthread_t clientTid;
    pthread_create(&clientTid, NULL, auction_client, parameters);
    pthread_detach(clientTid);
}

/* check_time()
 * ------------
 * Function for dedicated time thread which checks expiry time for every item
 * 	and sends corresponding message to clients if necessary.
 *
 * params: a null pointer to the struct containing all of program's data.
 *
 * Returns: an empty null pointer.
 */
void* check_time(void* params) {
    ProgramParameters* parameters = (ProgramParameters*) params;
    while (1) {
	// Check expiry times for every item.
	take_lock(parameters->lock);
	for (int i = 0; i < parameters->numOfItems; i++) {
	    if (get_time_ms() >= parameters->items[i].expiryTime) {
		ItemList item = parameters->items[i];	

		// Send sold or unsold message to seller.
		FILE* sellerOutput = item.seller.output;
		if (item.highestBidder == false) {
		    if (item.sellerActive) {
			fprintf(sellerOutput, ":unsold %s\n", item.item);
			fflush(sellerOutput);
		    }
		} else {
		    if (item.sellerActive) {
			fprintf(sellerOutput, ":sold %s %d\n", item.item,
				item.highestBid);
			fflush(sellerOutput);
		    }
		    
		    // Send won message to highest bidder.
		    Client highestBidder = item.topBidder;
		    if (item.bidderActive) {
			FILE* bidderOutput = highestBidder.output;
			fprintf(bidderOutput, ":won %s %d\n", item.item,
				item.highestBid);
			fflush(bidderOutput);
		    }
		}
		// Remove item from array.
		remove_item(parameters, i);
		parameters->numOfItems--;
	    }
	}
	release_lock(parameters->lock);
	usleep(100000);
    }
    return NULL;
}

/* remove_item()
 * -------------
 * Removes an item and all of its corresponding data from items data struct.
 *
 * parameters: a data struct containing all the data for the program.
 * itemIndex: the index of the item to remove in the item data struct.
 *
 * Returns: void
 */
void remove_item(ProgramParameters* parameters, int itemIndex) {
    // Remove item from item data struct.
    for (int i = itemIndex; i < parameters->numOfItems - 1; i++) {
	parameters->items[i].seller = parameters->items[i + 1].seller;
	parameters->items[i].item = strdup(parameters->items[i + 1].item);
	free(parameters->items[i + 1].item);
	parameters->items[i].reserve = parameters->items[i + 1].reserve;
	parameters->items[i].duration = parameters->items[i + 1].duration;
	parameters->items[i].highestBidder =
	        parameters->items[i + 1].highestBidder;
	parameters->items[i].highestBid = parameters->items[i + 1].highestBid;
	parameters->items[i].expiryTime = parameters->items[i + 1].expiryTime;
	parameters->items[i].topBidder = parameters->items[i + 1].topBidder;
    }
}

/* auction_client()
 * ----------------
 * A function for the thread for each client, which accepts client input
 * 	and executes command accordingly.
 *
 * params: a null pointer to the struct containing all of program's data.
 *
 * Returns: empty null pointer
 */
void* auction_client(void* params) {
    ProgramParameters* parameters = (ProgramParameters*) params;
    int clientIndex = parameters->numOfClients - 1;
    int clientFd = parameters->clients[clientIndex].clientFd;
    FILE* input = fdopen(clientFd, "r");
    FILE* output = fdopen(dup(clientFd), "w");
    parameters->clients[clientIndex].tid = pthread_self();
    parameters->clients[clientIndex].input = input;
    parameters->clients[clientIndex].output = output;

    // Read from client.
    char* line;
    while ((line = read_line(input))) {
	take_lock(parameters->lock);

	char** splitLine = split_by_char(line, ' ', 0);

	// Get number of words in text message from client.
	int length = 0;
	for (int i = 0; splitLine[i] != NULL; i++) {
	    length++;
	}

	// Check if input is valid.
	check_input(length, splitLine, parameters, output, clientIndex);

	// Release lock after line has been processed.
	release_lock(parameters->lock);
    }
    // Update that seller or bidder has left for each item.
    take_lock(parameters->lock);
    for (int i = 0; i < parameters->numOfItems; i++) {
	if (parameters->items[i].seller.tid == 
		parameters->clients[clientIndex].tid) {
	    parameters->items[i].sellerActive = false;
	}
	if (parameters->items[i].highestBidder 
		&& parameters->items[i].topBidder.tid ==
		parameters->clients[clientIndex].tid) {
	    parameters->items[i].bidderActive = false;
	}
    }
    --parameters->numOfActiveClients;
    release_lock(parameters->lock);
    fclose(input);
    fclose(output);
    close(clientFd);
    return NULL;
}

/* check_input()
 * -------------
 * Checks the input from client, validates it, and executes it.
 *
 * length: the number of words in the input command from client.
 * splitLine: an array of arrays of the input from client, split by ' '.
 * parameters: a data struct containing all the data for the program.
 * output: the output file descriptor of the client.
 * clientIndex: index of client in the array of client struct.
 * 
 * Returns: void
 */
void check_input(int length, char** splitLine, ProgramParameters* parameters,
	FILE* output, int clientIndex) {
    if (strcmp(splitLine[0], "sell") == 0) {
	if (length != 4) {
	    fprintf(output, ":invalid\n");
	} else {
	    // Validate item to sell
	    check_sell(splitLine, parameters,
		    parameters->clients[clientIndex]);
	}
    } else if (strcmp(splitLine[0], "bid") == 0) {
	if (length != 3) {
	    fprintf(output, ":invalid\n");
	} else {
	    // Validate item to bid
	    place_bid(splitLine, parameters,
		    parameters->clients[clientIndex]);
	}
    } else if (strcmp(splitLine[0], "list") == 0) {
	if (length != 1) {
	    fprintf(output, ":invalid\n");
	} else {
	    // List all items
	    list_all_items(parameters, output);
	}
    } else {
	fprintf(output, ":invalid\n");
    }

    fflush(output);
}

/* check_sell()
 * ------------
 * Checks if sell command is valid and places item for sale in corresponding
 * 	struct.
 *
 * splitLine: an array of arrays of the input from client, split by ' '.
 * parameters: a data struct containing all the data for the program.
 * client: a struct containing the client tid and output file descriptor.
 *
 * Returns: void
 */
void check_sell(char** splitLine, ProgramParameters* parameters,
	Client client) {
    FILE* outputClient = client.output;

    // Check if given reserve is valid.
    char* remainderText;
    int reserve = strtol(splitLine[2], &remainderText, 10);
    if (strlen(remainderText) != 0 || reserve < 0) {
	fprintf(outputClient, ":invalid\n");
	return;
    }

    // Check if given duration is valid.
    int duration = strtol(splitLine[3], &remainderText, 10);
    if (strlen(remainderText) != 0 || duration < 1) {
	fprintf(outputClient, ":invalid\n");
	return;
    }

    // Check if item is already on sale.
    char* item = splitLine[1];
    for (int i = 0; i < parameters->numOfItems; i++) {
	if (strcmp(parameters->items[i].item, item) == 0) {
	    fprintf(outputClient, ":rejected\n");
	    return;
	}
    }

    // Add item to list of items being sold.
    parameters->items = realloc(parameters->items, sizeof(ItemList)
	    * ++(parameters->numOfItems));
    int itemNum = parameters->numOfItems - 1;
    parameters->items[itemNum].seller = client;
    parameters->items[itemNum].sellerActive = true;
    parameters->items[itemNum].item = strdup(item);
    parameters->items[itemNum].reserve = reserve;
    parameters->items[itemNum].duration = duration;
    parameters->items[itemNum].highestBidder = false;
    parameters->items[itemNum].highestBid = 0;
    parameters->items[itemNum].expiryTime = get_time_ms() + duration;
    fprintf(outputClient, ":listed %s\n", parameters->items[itemNum].item);
}

/* list_all_items()
 * ----------------
 * Lists all the items available to bid.
 *
 * parameters: a data struct containing all the data for the program.
 * outputClient: the output file descriptor for the client to list the items
 * 	for.
 *
 * Returns: void
 */
void list_all_items(ProgramParameters* parameters, FILE* outputClient) {
    fprintf(outputClient, ":list ");
    for (int i = 0; i < parameters->numOfItems; i++) {
	ItemList* items = parameters->items;
	int remainingDuration = (int)(items[i].expiryTime - get_time_ms());
	fprintf(outputClient, "%s %d %d %d|", items[i].item, items[i].reserve,
		items[i].highestBid, remainingDuration);
    }
    fprintf(outputClient, "\n");
}

/* place_bid()
 * -----------
 * Checks if bid command is correct and places bid on specified item.
 *
 * splitLine: an array of arrays of the input from client, split by ' '.
 * parameters: a data struct containing all the data for the program.
 * client: a struct containing the client tid and output file descriptor.
 *
 * Returns: void
 */
void place_bid(char** splitLine, ProgramParameters* parameters,
	Client client) {

    bool valid = validate_bid_input(splitLine, parameters, client);
    if (!valid) {
	return;
    }

    FILE* outputClient = client.output;
    int bidAmount = strtol(splitLine[2], NULL, 10);

    // Get item ID
    int itemId = find_item(splitLine, parameters, outputClient);
    ItemList item = parameters->items[itemId];

    // Send outbid message to previous topBidder.
    if (item.highestBidder && item.bidderActive) {
	fprintf(item.topBidder.output, ":outbid %s %d\n",
		item.item, bidAmount);
	fflush(item.topBidder.output);
    }

    // Add client as highest bidder.
    parameters->items[itemId].highestBid = bidAmount;
    parameters->items[itemId].highestBidder = true;
    parameters->items[itemId].topBidder = client;
    parameters->items[itemId].bidderActive = true;

    fprintf(outputClient, ":bid %s\n", splitLine[1]);

}

/* validate_bid_input()
 * --------------------
 * Checks if the bid command from client is valid.
 *
 * splitLine: an array of arrays of the input from client, split by ' '.
 * parameters: a data struct containing all the data for the program.
 * client: a struct containing the client tid and output file descriptor.
 *
 * Returns: true if input is valid, but false if invalid.
 */
bool validate_bid_input(char** splitLine, ProgramParameters* parameters,
	Client client) {
    FILE* outputClient = client.output;

    // Check if bid value is valid.
    char* remainderText;
    int bidAmount = strtol(splitLine[2], &remainderText, 10);
    if (strlen(remainderText) != 0 || bidAmount < 1) {
	fprintf(outputClient, ":invalid\n");
	return false;
    }

    int itemId = find_item(splitLine, parameters, outputClient);
    if (itemId == -1) {
	return false;
    }

    // Check if client is placing a valid bid.
    if ((client.tid == parameters->items[itemId].seller.tid
            && parameters->items[itemId].sellerActive)
	    || bidAmount < parameters->items[itemId].reserve
	    || bidAmount <= parameters->items[itemId].highestBid) { 
	fprintf(outputClient, ":rejected\n");
	return false;
    }

    ItemList item = parameters->items[itemId];
    if (parameters->items[itemId].highestBidder != false) {
	Client highestBidder = parameters->items[itemId].topBidder;
	if (client.tid == highestBidder.tid && item.bidderActive) {
	    fprintf(outputClient, ":rejected\n");
	    return false;
	}
    }
    return true;
}

/* find_item()
 * -----------
 * Finds the index of the item in the bid input command.
 *
 * splitLine: an array of arrays of the input from client, split by ' '.
 * parameters: a data struct containing all the data for the program.
 * outputClient: the output file descriptor of the client.
 *
 * Returns: index of item in data struct, but returns -1 if not found.
 */
int find_item(char** splitLine, ProgramParameters* parameters,
	FILE* outputClient) {
    for (int i = 0; i < parameters->numOfItems; i++) {
	if (strcmp(splitLine[1], parameters->items[i].item) == 0) {
	    return i;
	}
    }
    fprintf(outputClient, ":rejected\n");
    return -1;
}

/* check_argc()
 * ------------
 * Checks if the number of command line arguments is valid.
 *
 * argc: the number of command line arguments.
 *
 * Errors: Exits with status 10 and usage error message if number of command
 * 	line arguments exceed 5, or if an arg is missing its value.
 */
void check_argc(int argc) {
    if (argc > MAX_ARGS || argc % 2 == 0) {
	fprintf(stderr, USAGE_ERR_MSG);
	exit(USAGE_ERR);
    }
}

/* check_valid_args()
 * ------------------
 * Checks if all the command line arguments are valid and are not repeated.
 *
 * argc: the number of command line arguments.
 * argv: an array of arrays containing the command line arguments.
 *
 * Errors: Exits with status 10 and usage error message if any argument is
 * 	invalid or repeated.
 */
void check_valid_args(int argc, char** argv) {
    // Iterate through all args in command line and check if each is valid.
    char* validArgs[NUM_OF_VALID_ARGS] = {MAXCONN, LISTENON};
    for (int i = 1; i < argc; i += 2) {
	int invalidCounter = 0;
	for (int j = 0; j < NUM_OF_VALID_ARGS; j++) {
	    if (strcmp(argv[i], validArgs[j]) != 0) {
		invalidCounter++;
	    }
	}
	if (invalidCounter > 1) {
	    fprintf(stderr, USAGE_ERR_MSG);
	    exit(USAGE_ERR);
	}
    }

    // Check for repeated args
    for (int i = 0; i < NUM_OF_VALID_ARGS; i++) {
	int repeatCounter = 0;
	for (int j = 1; j < argc; j += 2) {
	    if (strcmp(validArgs[i], argv[j]) == 0) {
		repeatCounter++;
	    }
	}
	if (repeatCounter > 1) {
	    fprintf(stderr, USAGE_ERR_MSG);
	    exit(USAGE_ERR);
	}
    }
}

/* get_num_connections()
 * ---------------------
 * Gets the value for num-connections argument from command line and checks
 * 	its validity.
 *
 * argc: the number of command line arguments.
 * argv: an array of arrays containing the command line arguments.
 * 
 * Returns: the num-connections arg specified in the command line, however it
 * 	returns -1 if it is not supplied.
 * Errors: Exits with status 10 and usage error message if the given value
 * 	is not a positive integer.
 */
int get_num_connections(int argc, char** argv) {
    for (int i = 1; i < argc; i += 2) {
	if (strcmp(argv[i], MAXCONN) == 0) {
	    // Try converting the num-connections arg to an integer.
	    char* remainderText;
	    int numConnections = strtol(argv[i + 1], &remainderText, 10);
	    if (strlen(remainderText) != 0 || numConnections < 0) {
		fprintf(stderr, USAGE_ERR_MSG);
		exit(USAGE_ERR);
	    }
	    return numConnections;
	}
    }
    return -1;
}

/* get_port_number()
 * -----------------
 * Gets the value for port number argument from command line and checks
 * 	its validity.
 *
 * argc: the number of command line arguments.
 * argv: an array of arrays containing the command line arguments.
 * 
 * Returns: the port number arg specified in the command line, however it
 * 	returns 0 if it is not supplied.
 * Errors: Exits with status 10 and usage error message if the given value
 * 	is not an integer or if not between 1024 and 65535 and not 0.
 */
const char* get_port_number(int argc, char** argv) {
    for (int i = 1; i < argc; i += 2) {
	if (strcmp(argv[i], LISTENON) == 0) {
	    // Check if port number is an integer.
	    char* remainderText;
	    int portNumber = strtol(argv[i + 1], &remainderText, 10);
	    if (strlen(remainderText) != 0) {
		fprintf(stderr, USAGE_ERR_MSG);
		exit(USAGE_ERR);
	    }

	    // Check if port number is a valid number.
	    if (portNumber < MIN_PORT || portNumber > MAX_PORT) {
		if (portNumber != 0) {
		    fprintf(stderr, USAGE_ERR_MSG);
		    exit(USAGE_ERR);
		}
	    }
	    return argv[i + 1];
	}
    }
    return DEFAULT_PORT;
}

/* create_socket()
 * ---------------
 * Creates a socket with the specified port number and makes a file descriptor
 * 	for it.
 *
 * portNumber: the portnumber specified in the command line, or ephemeral
 * 	port number.
 *
 * Returns: the file descriptor of the socket.
 * Errors: Exits with status 17 and connect error message if it cannot connect
 * 	to the port.
 */
int create_socket(const char* portNumber) {
    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE; // Bind with all interfaces

    // Try finding address of port.
    int error;
    if ((error = getaddrinfo("localhost", portNumber, &hints, &ai))) {
	freeaddrinfo(ai);
	fprintf(stderr, PORT_CONNECT_ERR_MSG);
	exit(PORT_CONNECT_ERR);
    }

    // Create socket and bind to a port.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(fd, ai->ai_addr, sizeof(struct sockaddr))) {
	fprintf(stderr, PORT_CONNECT_ERR_MSG);
	exit(PORT_CONNECT_ERR);
    }

    // Make port reusable.
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    return fd;
}

/* print_port_and_listen()
 * -----------------------
 * Prints the port number and starts listening for connections from clients.
 *
 * parameters: a data struct containing all the data for the program.
 *
 * Errors: Exits with status 17 and connect error message if it cannot connect
 * 	to client.
 */
void print_port_and_listen(ProgramParameters* parameters) {
    // Check which port was given.
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    if (getsockname(parameters->socketFd, (struct sockaddr*) &ad, &len)) {
	fprintf(stderr, PORT_CONNECT_ERR_MSG);
	exit(PORT_CONNECT_ERR);
    }

    fprintf(stderr, "%d\n", ntohs(ad.sin_port));
    fflush(stderr);

    // Listen to port and specify max number of connections.
    if (listen(parameters->socketFd, parameters->numConnections)) {
	fprintf(stderr, PORT_CONNECT_ERR_MSG);
	exit(PORT_CONNECT_ERR);
    }
}
