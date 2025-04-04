let username;
while (!username || username.trim() === "") 
{
    username = prompt("Enter your username:");
}


document.getElementById('username-display').textContent = username;

let today = new Date();
let dateStr = today.toLocaleDateString();
document.getElementById('date-display').textContent = dateStr;

let socket = new WebSocket("ws://localhost:9000");
socket.onopen = function() 
{
            console.log("Connected!");
            socket.send("SETNAME:" + username);
};
socket.onmessage = function(event)
{
    let message = event.data;
    let chatBox = document.getElementById('chat-box');
    let msgDiv = document.createElement("div");
    msgDiv.classList.add("message");

    if (message.startsWith("ERROR:")) 
    {
        alert(message); // Show error message
        socket.close(); // Close connection
        setTimeout(() => location.reload(), 1000); // Reload page for new username
        return;
    }

    if (message.startsWith("[Private]")) 
    {
        msgDiv.classList.add("private-message");
     } 
    else if (message.startsWith(username + ":")) 
    {
        msgDiv.classList.add("user-message");
    } 
    else 
    {
         msgDiv.classList.add("server-message");
    }

            msgDiv.textContent = message;
            chatBox.appendChild(msgDiv);
            chatBox.scrollTop = chatBox.scrollHeight;
 };

 socket.onerror = function(error) 
{
    console.error("WebSocket Error:", error);
    alert("WebSocket connection error. Check if the server is running.");
};

socket.onclose = function() 
{
     console.warn("WebSocket closed.");
     alert("Disconnected from the server.");
};

function sendMessage() 
{
    let input = document.getElementById('message');
    let msg = input.value.trim();
     if (msg) 
    {
        socket.send(msg);
        addMessageToChat("You: " + msg, "user-message");
        input.value = '';
    }
 }
function addMessageToChat(message, type) 
 {
     let chatBox = document.getElementById('chat-box');
     let msgDiv = document.createElement("div");
     msgDiv.classList.add("message", type);
     msgDiv.textContent = message;
     chatBox.appendChild(msgDiv);
    chatBox.scrollTop = chatBox.scrollHeight;
}
document.getElementById('message').addEventListener("keypress", function(event) 
    {
        if (event.key === "Enter") 
        {
           event.preventDefault();
            sendMessage();
        }
    });