// This project uses an ADAFruit CC3000 WiFi Shield to listen
// for a GET request with a specific password embedded.
// When recieved we activate a servo to push an normal remote 
// control button to do whatever --> in this case it pushes a
// Gate Remote to be able to toggle the gate via WiFi.

#include <Adafruit_CC3000.h>
#include <SPI.h>
#include "utility/debug.h"
#include "utility/socket.h"
#include <Servo.h>

// ----- WiFi -----

// These are the interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3  // MUST be an interrupt pin!
// These can be any two pins
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10
// Use hardware SPI for the remaining pins
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11

Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT, SPI_CLOCK_DIVIDER);

#define WLAN_SSID       "YOURWIFISSID"   // cannot be longer than 32 characters!
#define WLAN_PASS       "YOURPASSWORD"

// Security can be WLAN_SEC_UNSEC, WLAN_SEC_WEP, WLAN_SEC_WPA or WLAN_SEC_WPA2
#define WLAN_SECURITY   WLAN_SEC_WPA2

#define LISTEN_PORT           80      // What TCP port to listen on for connections.  
                                      // The HTTP protocol uses port 80 by default.

#define MAX_ACTION            10      // Maximum length of the HTTP action that can be parsed.

#define MAX_PATH              64      // Maximum length of the HTTP request path that can be parsed.
                                      // There isn't much memory available so keep this short!

#define BUFFER_SIZE           MAX_ACTION + MAX_PATH + 20  // Size of buffer for incoming request data.
                                                          // Since only the first line is parsed this
                                                          // needs to be as large as the maximum action
                                                          // and path plus a little for whitespace and
                                                          // HTTP version.

#define TIMEOUT_MS            500    // Amount of time in milliseconds to wait for
                                     // an incoming request to finish.  Don't set this
                                     // too high or your server could be slow to respond.

Adafruit_CC3000_Server httpServer(LISTEN_PORT);
uint8_t buffer[BUFFER_SIZE+1];
int bufindex = 0;
char action[MAX_ACTION+1];
char path[MAX_PATH+1];

// ----- Servo -----
Servo myservo;
#define restPos 0
#define switchPos 10
#define servoPin 9

// ----- Button -----
#define buttonPin 6

void setup(void)
{
  Serial.begin(115200);
  
  // Set Servo to desired location and release it later
  // If you don't release it, I noticed annoying server jitter that takes over
  myservo.attach(servoPin);
  myservo.write(restPos);

  // Set the button pin
  pinMode(buttonPin, INPUT);

  // We can remove this if you don't need the debug info
  Serial.print("Free RAM: "); 
  Serial.println(getFreeRam(), DEC);
  
  // Initialise the Wifi module
  Serial.println(F("Initializing..."));
  if (!cc3000.begin())
  {
    Serial.println(F("Couldn't begin()! Check wiring?"));
    while(1);
  }
  
  Serial.print(F("Connecting to ")); Serial.println(WLAN_SSID);
  if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    Serial.println(F("Failed!"));
    while(1);
  }
   
  Serial.println(F("Connected!"));
  
  Serial.println(F("Requesting DHCP"));
  while (!cc3000.checkDHCP())
  {
    delay(100); // ToDo: Insert a DHCP timeout!
  }  

  // Display the IP address DNS, Gateway, etc.
  while (! displayConnectionDetails()) {
    delay(1000);
  }
  
  // Start listening for connections
  httpServer.begin();
  
  Serial.println(F("Listening..."));

  // detach the servo to prevent 'Servo Jitter'
  myservo.detach();
}

void loop(void)
{
  // Try to get a client which is connected.
  Adafruit_CC3000_ClientRef client = httpServer.available();
  if (client) {
    Serial.println(F("Client connected."));
    // Process this request until it completes or times out.
    // Note that this is explicitly limited to handling one request at a time!

    // Clear the incoming data buffer and point to the beginning of it.
    bufindex = 0;
    memset(&buffer, 0, sizeof(buffer));
    
    // Clear action and path strings.
    memset(&action, 0, sizeof(action));
    memset(&path,   0, sizeof(path));

    // Set a timeout for reading all the incoming data.
    unsigned long endtime = millis() + TIMEOUT_MS;
    
    // Read all the incoming data until it can be parsed or the timeout expires.
    bool parsed = false;
    while (!parsed && (millis() < endtime) && (bufindex < BUFFER_SIZE)) {
      if (client.available()) {
        buffer[bufindex++] = client.read();
      }
      parsed = parseRequest(buffer, bufindex, action, path);
    }

    // Handle the request if it was parsed.
    if (parsed) {
      Serial.println(F("Processing request"));
      Serial.print(F("Action: ")); Serial.println(action);
      Serial.print(F("Path: ")); Serial.println(path);
      // Check the action to see if it was a GET request.
      if (strcmp(action, "GET") == 0) {
        // Respond with the path that was accessed.
        // First send the success response code.
        client.fastrprintln(F("HTTP/1.1 200 OK"));
        // Then send a few headers to identify the type of data returned and that
        // the connection will not be held open.
        client.fastrprintln(F("Content-Type: text/plain"));
        client.fastrprintln(F("Connection: close"));
        client.fastrprintln(F("Server: Home Gate"));
        // Send an empty line to signal start of body.
        client.fastrprintln(F(""));
        // Now send the response data....
        client.fastrprint(F("Accessed: ")); client.fastrprintln(path);

        // --------------  Check the content to 'authenticate'
        // Check password / pin e.g. 1234 at chars 13-14
        // For example your GET request could look like this ?togglegate=1234
        if ((path[13] == '1') && (path[14] == '2') && (path[15] == '3') && (path[16] == '4')) {
          Serial.println("Access Granted.");
          pushButton();     // Do the servo thing
        }
      }
      else {
        // Unsupported action, respond with an HTTP 405 method not allowed error.
        client.fastrprintln(F("HTTP/1.1 405 Method Not Allowed"));
        client.fastrprintln(F(""));
      }
    }

    // Wait a short period to make sure the response had time to send before
    // the connection is closed (the CC3000 sends data asyncronously).
    delay(100);

    // Close the connection when done.
    Serial.println(F("Client disconnected"));
    client.close();
  }

  // We check if the button on the breadboard was pressed
  if (digitalRead(buttonPin) == HIGH) {
    Serial.println("Button Pressed");
    pushButton();
    delay(100);
  }
}

// This function pushes the button using a the servo.
void pushButton() {
  myservo.attach(servoPin);
  Serial.print("push->");
  myservo.write(switchPos);
  delay(200);
  
  Serial.print("release->");
  myservo.write(restPos);
  delay(200);
  
  // Detach the servo, prevent servo jitter
  myservo.detach();
  Serial.println("..done");
}

// Return true if the buffer contains an HTTP request.  Also returns the request
// path and action strings if the request was parsed.  This does not attempt to
// parse any HTTP headers because there really isn't enough memory to process
// them all.
// HTTP request looks like:
//  [method] [path] [version] \r\n
//  Header_key_1: Header_value_1 \r\n
//  ...
//  Header_key_n: Header_value_n \r\n
//  \r\n
bool parseRequest(uint8_t* buf, int bufSize, char* action, char* path) {
  // Check if the request ends with \r\n to signal end of first line.
  if (bufSize < 2)
    return false;
  if (buf[bufSize-2] == '\r' && buf[bufSize-1] == '\n') {
    parseFirstLine((char*)buf, action, path);
    return true;
  }
  return false;
}

// Parse the action and path from the first line of an HTTP request.
void parseFirstLine(char* line, char* action, char* path) {
  // Parse first word up to whitespace as action.
  char* lineaction = strtok(line, " ");
  if (lineaction != NULL)
    strncpy(action, lineaction, MAX_ACTION);
  // Parse second word up to whitespace as path.
  char* linepath = strtok(NULL, " ");
  if (linepath != NULL)
    strncpy(path, linepath, MAX_PATH);
}

// Tries to read the IP address and other connection details
bool displayConnectionDetails(void)
{
  uint32_t ipAddress, netmask, gateway, dhcpserv, dnsserv;
  
  if(!cc3000.getIPAddress(&ipAddress, &netmask, &gateway, &dhcpserv, &dnsserv))
  {
    Serial.println(F("Unable to retrieve the IP Address!\r\n"));
    return false;
  }
  else
  {
    Serial.print(F("IP Addr: ")); cc3000.printIPdotsRev(ipAddress);
    Serial.println();

    return true;
  }
}
