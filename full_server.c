#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#define SERVER_PORT 9000
#define MAX_MESSAGE_SIZE 256
#define MAX_NAME_LEN 32

struct client_node 
{
    struct lws *wsi;
    char username[MAX_NAME_LEN];
    char pending_message[MAX_MESSAGE_SIZE];  // Store messages per client
    struct client_node *next;
};

static struct client_node *clients = NULL;
static int interrupted = 0;

void signal_handler(int sig) {
    interrupted = 1;
}

struct client_node *find_client_by_name(const char *username) {
    struct client_node *current = clients;
    while (current) 
    {
        if (strcmp(current->username, username) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void add_client(struct lws *wsi, const char *username) // adding new clients in a linked list
{
    if (find_client_by_name(username))  // every client must have unique uername
    {
        lwsl_user("Username %s is already taken.\n", username);
        char msg[MAX_MESSAGE_SIZE];
        snprintf(msg, sizeof(msg), "ERROR: Username %s is already in use. Choose another.", username);
        lws_write(wsi, (unsigned char *)msg, strlen(msg), LWS_WRITE_TEXT);
        return;
    }

    struct client_node *new_client = malloc(sizeof(struct client_node));
    if (!new_client) 
    {
        lwsl_err("Memory allocation failed\n");
        return;
    }

    strncpy(new_client->username, username, MAX_NAME_LEN - 1);
    new_client->username[MAX_NAME_LEN - 1] = '\0';
    new_client->wsi = wsi;
    new_client->next = clients;
    clients = new_client;

    lwsl_user("New client connected: %s\n", username);
}

void remove_client(struct lws *wsi) 
{
    struct client_node **curr = &clients;
    while (*curr) {
        if ((*curr)->wsi == wsi) 
        {
            struct client_node *temp = *curr;
            *curr = (*curr)->next;
            free(temp);
            break;
        }
        curr = &((*curr)->next);
    }
}

void broadcast_message(struct lws *sender, const char *message) 
{
    struct client_node *curr = clients;
    while (curr) 
    {
        if (curr->wsi != sender) 
        {
            strncpy(curr->pending_message, message, MAX_MESSAGE_SIZE - 1);
            curr->pending_message[MAX_MESSAGE_SIZE - 1] = '\0';
            lws_callback_on_writable(curr->wsi);
        }
        curr = curr->next;
    }
}

static int callback_server(struct lws *wsi, enum lws_callback_reasons reason,void *user, void *in, size_t len)
{
    switch (reason) 
    {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("Client connected\n");
            break;

        case LWS_CALLBACK_RECEIVE: 
        {
            char message[MAX_MESSAGE_SIZE];
            size_t copy_length = len < MAX_MESSAGE_SIZE - 1 ? len : MAX_MESSAGE_SIZE - 1;
            memcpy(message, in, copy_length);
            message[copy_length] = '\0';

            lwsl_user("Received message: \"%s\"\n", message);

            if (strncmp(message, "SETNAME:", 8) == 0) 
            {
                char username[MAX_NAME_LEN];
                strncpy(username, message + 8, sizeof(username) - 1);
                username[sizeof(username) - 1] = '\0';
                add_client(wsi, username);
                lwsl_user("Client set username: \"%s\"\n", username);
            } 
            else if (message[0] == '@') 
            {
                char recipient[MAX_NAME_LEN], private_message[MAX_MESSAGE_SIZE - 20];
                
                if (sscanf(message, "@%31s %235[^\n]", recipient, private_message) == 2) 
                {
                    struct client_node *rcpt = find_client_by_name(recipient);
                    if (rcpt) 
                    {
                        snprintf(rcpt->pending_message, MAX_MESSAGE_SIZE - 1, "[Private] %s", private_message);
                        lwsl_user("Private message to %s: \"%s\"\n", recipient, private_message);
                        lws_callback_on_writable(rcpt->wsi);
                    } 
                    else 
                    {
                        lwsl_user("Recipient \"%s\" not found.\n", recipient);
                    }
                }
            } 
            else 
            {
                char formatted_message[MAX_MESSAGE_SIZE];
                snprintf(formatted_message, MAX_MESSAGE_SIZE - 1, "[Public] %.240s", message);

                lwsl_user("Broadcasting message: \"%s\"\n", formatted_message);
                broadcast_message(wsi, formatted_message);
            }
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: 
        {
            struct client_node *curr = clients;
            while (curr) 
            {
                if (curr->wsi == wsi && curr->pending_message[0] != '\0') 
                {
                    unsigned char buffer[LWS_PRE + MAX_MESSAGE_SIZE];
                    unsigned char *output = &buffer[LWS_PRE];
                    memset(output, 0, MAX_MESSAGE_SIZE);
                    strcpy((char *)output, curr->pending_message);
                    lws_write(wsi, output, strlen(curr->pending_message), LWS_WRITE_TEXT);
                    curr->pending_message[0] = '\0';
                    break;
                }
                curr = curr->next;
            }
            break;
        }

        case LWS_CALLBACK_CLOSED:
            lwsl_user("Client disconnected\n");
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

int main(void) {
    signal(SIGINT, signal_handler);
    printf("Starting WebSocket server on port %d\n", SERVER_PORT);
    lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE, NULL);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = SERVER_PORT;
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) 
    {
        printf("Failed to create WebSocket context\n");
        return -1;
    }

    while (!interrupted)
        lws_service(context, 50);

    lws_context_destroy(context);
    return 0;
}
