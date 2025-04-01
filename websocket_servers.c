#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#define SERVER_PORT 9000
#define MAX_MESSAGE_SIZE 128
#define MAX_QUEUE_SIZE 10  // Store up to 10 messages in the queue

// Linked list for connected clients
struct client_node {
    struct lws *wsi;
    struct client_node *next;
};

static struct client_node *clients = NULL;
static int interrupted = 0;

// Message queue for storing chat messages
static char message_queue[MAX_QUEUE_SIZE][MAX_MESSAGE_SIZE];
static int queue_start = 0, queue_end = 0, queue_size = 0;

void signal_handler(int sig) {
    interrupted = 1;
}

// Add client to list
void add_client(struct lws *wsi) {
    struct client_node *new_client = (struct client_node *)malloc(sizeof(struct client_node));
    new_client->wsi = wsi;
    new_client->next = clients;
    clients = new_client;
}

// Remove client from list
void remove_client(struct lws *wsi) {
    struct client_node **curr = &clients;
    while (*curr) {
        if ((*curr)->wsi == wsi) {
            struct client_node *temp = *curr;
            *curr = (*curr)->next;
            free(temp);
            break;
        }
        curr = &((*curr)->next);
    }
}

// Add message to the queue
void enqueue_message(const char *message) {
    if (queue_size == MAX_QUEUE_SIZE) {
        // Queue full, remove oldest message
        queue_start = (queue_start + 1) % MAX_QUEUE_SIZE;
        queue_size--;
    }
    strncpy(message_queue[queue_end], message, MAX_MESSAGE_SIZE - 1);
    message_queue[queue_end][MAX_MESSAGE_SIZE - 1] = '\0';
    queue_end = (queue_end + 1) % MAX_QUEUE_SIZE;
    queue_size++;
}

// Get the oldest message from the queue
const char *dequeue_message() {
    if (queue_size == 0) return NULL;
    const char *message = message_queue[queue_start];
    queue_start = (queue_start + 1) % MAX_QUEUE_SIZE;
    queue_size--;
    return message;
}

// Broadcast message to all clients
void broadcast_message(struct lws *sender, const char *message) {
    enqueue_message(message); // Store the message

    struct client_node *curr = clients;
    while (curr) {
        if (curr->wsi != sender) { // Don't send back to sender
            lws_callback_on_writable(curr->wsi);
        }
        curr = curr->next;
    }
}

static int callback_server(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len)
{
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("Client connected\n");
            printf("Client connected\n");
            add_client(wsi);
            break;

        case LWS_CALLBACK_RECEIVE: {
            char message[MAX_MESSAGE_SIZE];
            size_t copy_length = len < MAX_MESSAGE_SIZE - 1 ? len : MAX_MESSAGE_SIZE - 1;
            memcpy(message, in, copy_length);
            message[copy_length] = '\0';

            lwsl_user("Received from client: %s\n", message);
            printf("Received from client: %s\n", message);

            // Broadcast to all clients except sender
            broadcast_message(wsi, message);
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            const char *msg = dequeue_message();
            if (msg) {
                unsigned char buffer[LWS_PRE + MAX_MESSAGE_SIZE];
                unsigned char *output = &buffer[LWS_PRE];

                memset(output, 0, MAX_MESSAGE_SIZE);
                strcpy((char *)output, msg);

                int bytes_written = lws_write(wsi, output, strlen(msg), LWS_WRITE_TEXT);
                if (bytes_written < 0) {
                    lwsl_err("Failed to send response\n");
                } else {
                    lwsl_user("Broadcasted message: %s\n", msg);
                    printf("Broadcasted message: %s\n", msg);
                }
            }
            break;
        }

        case LWS_CALLBACK_CLOSED:
            lwsl_user("Client disconnected\n");
            printf("Client disconnected\n");
            remove_client(wsi);
            break;

        default:
            break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    { "websocket-protocol", callback_server, 0, MAX_MESSAGE_SIZE },
    { NULL, NULL, 0, 0 }
};

int main(void)
{
    lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE, NULL);
    lwsl_user("Starting WebSocket server on port %d\n", SERVER_PORT);
    printf("Starting WebSocket server on port %d\n", SERVER_PORT);
    signal(SIGINT, signal_handler);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = SERVER_PORT;
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        lwsl_err("Failed to create WebSocket context\n");
        return -1;
    }

    while (!interrupted)
        lws_service(context, 50);  // Process events every 50 ms

    lws_context_destroy(context);
    return 0;
}
