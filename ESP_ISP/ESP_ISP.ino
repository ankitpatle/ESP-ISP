/******************************************************************************************************************************************************************
ESP-ISP (ESP32 based ISP for Atmel 8051 controllers)
Copyright (C) 2020  Ankit Patle

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*******************************************************************************************************************************************************************/
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>
#include <SPI.h>

/*************************************************************Global Variable and Objects Declaration*************************************************************/
const char *sta_ssid = "****";        //Station SSID and Password
const char *sta_pass = "****";        //Can be left as it is, will be changed from webpage

const char *ap_ssid = "ESP_AP";       //Access Point ID and password
const char *ap_pass = "12345678";

static const int spiClk = 100000;     // 500 kHz, 11.0592Mhz/16 rounded off(8051 clock)
SPIClass * hspi = NULL;               //Use HSPI

bool checked = 0;                     //variable for checking if already programming enable is checked

char wtype = -1;                      //Used for checking if socket is opened for programming(0), reading(1), fast programming(2)

int file_index = 1;                   //Used to keep track of file index
int mem_len = 0;                      //Number of bytes to read from microcontroller

AsyncWebServer server(80);            // Create a webserver object that listens for HTTP request on port 80

WebSocketsServer webSocket = WebSocketsServer(1337);    //Using port 1337 for websocket (for browser based function)
WebSocketsServer webSocket2 = WebSocketsServer(1339);   //Using port 1339 for websocket (for direct functions)
/*******************************************************************************************************************************************************************/

/*****************************************************************All Function Declaration**************************************************************************/
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);        //Upload a new file to the SPIFFS
void handleTempFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);    //Upload a temp file to RAM
bool handleFile(String path);                                                                                                       //Request to download any hex
char file_read(File file, int pos);                                                                                                 //Byte file read

int strhex2int(char h, char l);                                                                                                     //Convert HEX string to int
void chip_erase();                                                                                                                  //8051 erase function
void write_byte(int address, char dat);                                                                                             //8051 write byte function
char read_byte(int address);                                                                                                        //8051 read byte function
bool verify_byte(int address, char dat);                                                                                            //8051 verify byte function

void handle_program(AsyncWebServerRequest *request);                                                                                //Program the microcontroller
void handle_fprog(AsyncWebServerRequest *request);                                                                                  //Program the microcontroller(Using temprorary file)
void handle_reading(AsyncWebServerRequest *request);                                                                                //Read the microcontroller
void handle_verification(AsyncWebServerRequest *request);                                                                           //Verify flash with file
void handle_finf(AsyncWebServerRequest *request);                                                                                   //Send file info to client
void handle_checkconn(AsyncWebServerRequest *request);                                                                              //Check  and send connection info
bool handle_ssid(AsyncWebServerRequest *request);                                                                                   //Receive ssid and connect to network

String file_search(char x);                                                                                                         //Search hex file by index and return name, \0 if no file
bool file_delete(String path);                                                                                                      //delete hex file by name

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);                                                  //Websocket 1 event function
void webSocket2Event(uint8_t num, WStype_t type, uint8_t * payload, size_t length);                                                 //Websocket 2 event function
/********************************************************************************************************************************************************************/

/******************************************************************Setup Function************************************************************************************/
void setup() 
{
  Serial.begin(115200);
  Serial.setTimeout(500);                                       //Serial timeout is set too 500 ms
  Serial.setRxBufferSize(8192);                                 //Serial Buffer sixe is set to 8192 bytes

  WiFi.softAP(ap_ssid, ap_pass);
  WiFi.mode(WIFI_AP_STA);
      
  MDNS.addService("_http","_tcp",80);
  if (!MDNS.begin("esp-isp"));                                  // Start the mDNS responder for esp.local
    //Serial.println("Error setting up MDNS responder!");
  //else
    //Serial.println("mDNS responder started");
  
  SPIFFS.begin();                                              // Start the SPI Flash Files System

  hspi = new SPIClass(HSPI);                                   //initialise instance of the SPIClass attached to HSPI
  hspi->begin(14, 26, 13, 27);                                 //initialise hspi with alternate pin hspi->begin(14, 12, 13, 15); //here-> SCLK(14), MISO(26), MOSI(13), SS(27)
  pinMode(27, OUTPUT);                                         //HSPI SS, set up slave select pin as output


  server.on("/", HTTP_GET, []                                  //If the client requests the upload page
  (AsyncWebServerRequest *request)            
  {                
    //Serial.println("Req = /");
    request->send(SPIFFS, "/index.html", "text/html");         //Send index
  });

 server.on("/test", HTTP_GET, []                               // test if connection is ok
  (AsyncWebServerRequest *request)
  {
    //Serial.println("Req = /test");
    request->send(200, "text/plain", "OK");
  });

  server.on("/upload", HTTP_POST, []
    (AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
    {handleFileUpload(request, filename, index, data, len, final);}
  );

  server.on("/tempfile", HTTP_POST, []
    (AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
    {handleTempFileUpload(request, filename, index, data, len, final);}
  );
 
  server.on("/setup", HTTP_GET, []                          //If the client requests the upload page
  (AsyncWebServerRequest *request)
  {                
    //Serial.println("Req = /setup");
    request->send(SPIFFS, "/setup.html", "text/html");      //Send setup
  });

  server.on("/program", HTTP_GET, handle_program );         //Handle for programming request 

  server.on("/fprog", HTTP_GET, handle_fprog );             //Handle for fast programming request 

  server.on("/read", HTTP_GET, handle_reading );            //Handle for reading request 

  server.on("/getfinf", HTTP_GET, handle_finf );            //Handle for file info check request

  server.on("/checkconn", HTTP_GET, handle_checkconn );     //Handle for connection check request

  server.on("/v_req", HTTP_GET, handle_verification );      //Handle for verify request

  server.on("/loc", HTTP_GET, []                            //Handle for location set request
  (AsyncWebServerRequest *request)
  {
    //Serial.println("Req = /loc");
    //Serial.println("arg = " + request->arg("location"));
    if (request->hasArg("location"))                        //Set the location as recieved
      file_index = request->arg("location")[0] - 48;
    request->send(200, "text/plain", String(file_index));
  });

  server.on("/ssid", HTTP_GET, handle_ssid );               //Handle for ssid setup request

  server.on("/open", HTTP_GET, []                           //If the client requests the upload page
  (AsyncWebServerRequest *request)            
  {   
    int a;             
    //Serial.println("Req = /open");
    if (request->hasArg("index"))
    {
      a = String(request->arg("index")).toInt();
      if(file_search(a) != '\0')
        request->send(SPIFFS, file_search(a), "text/plain");  //Send file as plain text
        
      else
        request->send(404, "text/plain", "404: Not Found");   //If not found respond with a 404 (Not Found) error
    }
  });

  server.onNotFound([]                                      //If the client requests any other URI
  (AsyncWebServerRequest *request) 
  {           
    String temp = request->url();
    //Serial.println("Req = not found -> " + temp);        
    request->send(404, "text/plain", "404: Not Found");     //If not found respond with a 404 (Not Found) error
  });


  server.begin();                                          // Actually start the server
  webSocket.begin();                                       //Start websocket 1 server
  webSocket.onEvent(webSocketEvent);                       //When websocket 1 is connected
  webSocket2.begin();                                      //Start websocket 2 server
  webSocket2.onEvent(webSocket2Event);                     //When websocket 2 is connected
}
/********************************************************************************************************************************************************************/

/******************************************************************Arduino Loop Funtion******************************************************************************/
void loop()
{
  webSocket.loop();
  webSocket2.loop();
  String ser;
  if(Serial.available())
  {  
    ser = Serial.readString();
    if(ser.substring(0, 6) == "FORMAT")                                     //SPIFFS Format command check
    {
      Serial.println("Formatting SPIFFS.....");
      if(SPIFFS.format())
        Serial.println("Format Complete");
      else
        Serial.println("Format Error");
    }
    
    else if (ser.substring(0, 5) == "proge")                                //At serial start do a programming ensble routine
    {
        byte recieved = 0;
        hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));   //Start spi transaction
    
        digitalWrite(27, HIGH);                                             //8051 needs reset high to program
        delay(1);
      
        hspi->transfer(0b10101100);                                         //Send programming Enable Command and recieve ack byte
        hspi->transfer(0b01010011);
        hspi->transfer(0b00000000);
        recieved = hspi->transfer(0b00000000);
    
        if(recieved == 0b01101001)
        {
          Serial.println("ok");                                             //If microcontroller replies with ack then send ok
        }
        else
        {
          Serial.println("err");                                            //Else err
          hspi->endTransaction();                                           //End transaction
        }
    }
    
    else if(ser.substring(0, 5) == "erase")                                 //If Erase command is received
    {
      chip_erase();
      Serial.println("done");
    }
    
    else if((ser.charAt(0) == 'w') && (ser.charAt(2) == ':'))               //If string to write is received
    {                                                                       //--Format is "wn:string" where n is index for each data segment of 2048 bytes
      int dat, j=0;                                                         //--String consists of byte data in hex format (max length of 1024 bytes as data) 
      int index = int(ser.charAt(1));                                       //Find index
      int starting = index * 1024;                                          //Set the starting address
      int ending = (ser.length() - 3)/2 + starting;                         //Set the ending address
      for(int i = starting; i < ending; i++)
      {
        dat = strhex2int(ser.charAt(j+3), ser.charAt(j+4));                 //Convert string hex to int
        write_byte(i, dat);                                                 //Write to microcontroller
        j += 2;
      }
     Serial.println("ok");                                                  //Reply with ok
    }
    
    else if((ser.charAt(0) == 'r') && (ser.charAt(1) == ':'))               //If read is requested
    {
      String dat;
      char hex_dat[4];
      int sepr = ser.lastIndexOf(':');                                      //Find seprating ':'
      int len = (ser.substring(sepr+1)).toInt();                            //Find length in string
      if(len>4096)
      {
        for(int i = 0; i < 4096; i++)
        {
          sprintf(hex_dat, "%02x", (int)read_byte(i));  
          dat += String(hex_dat);                                           //Read the data 
        }
        Serial.println(dat);                                                //Reply with data part 1
        dat = "";
        for(int i = 4096; i < len; i++)
        {
          sprintf(hex_dat, "%02x", (int)read_byte(i));  
          dat += String(hex_dat);                                           //Read the data 
        }
        Serial.println(dat);                                                //Reply with data part 2
      }
      else
      {
        for(int i = 0; i < len; i++)
        {
          sprintf(hex_dat, "%02x", (int)read_byte(i));  
          dat += String(hex_dat);                                           //Read the data     
        }
        Serial.println(dat);                                             //Reply with data
      }
    }
    else if(ser.substring(0, 5) == "disc")                                  //If disconnected
    {
      hspi->endTransaction();   //end transaction
      digitalWrite(27, LOW);
    }
    else 
      Serial.println("unknown command");
  }
  
}
/*********************************************************************************************************************************************************************/

/*******************************************************Uploaded File handling Function*******************************************************************************/
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) 
{                                                                                     //Upload a new file to the SPIFFS
  //Serial.println("\nReq = /upload");
  
  String tmp_filename = filename;
  File file;                                                                          //Create a file object

  tmp_filename = "/x"+ String(file_index) + "_" + tmp_filename;                       //Add "x" and "index" to all uploaded hex files
  //Serial.println(tmp_filename);
  
  /*if(!index)
  {
    Serial.println((String)"UploadStart: " + filename);
  }
  for(size_t i=0; i<len; i++)
  {
    Serial.write(data[i]);
  }
  if(final)
  {
    Serial.println((String)"\nUploadEnd: " + filename + "," + index+len);
  }*/
  
  if(!index)                                                                          //If it is first part of file then...
  {
    if(file_delete(file_search(file_index)))                                          //If file is present at index then delete it
      //Serial.println("\nfile deleted at" + String(file_index));
    
    file = SPIFFS.open(tmp_filename, "w");                                            //Open the file for writing in SPIFFS (overwrite any available)
  }
  else
    file = SPIFFS.open(tmp_filename, "a+");                                           //Open the file for writing in SPIFFS (create if it doesn't exist)

  file.write(data, len);                                                              //Write the file
  
  if(file) 
  {                                                                                   //If the file was successfully created
    file.close();                                                                     //Close the file     
    request->send(SPIFFS, "/success.html", "text/html");                              //Redirect the client to the success page
    request->send(303);
  }
  else 
  {
    request->send(500, "text/plain", "500: couldn't create file");
  }
}
/**********************************************************************************************************************************************************************/

/****************************************************Uploaded Temporary File handling Function*************************************************************************/
void handleTempFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) 
{                                                                                     //Upload a temp file to RAM
  //Serial.println("\nReq = /tempfile");
  //Serial.println(filename);
  
  String tmp_filename = "/temp.hex";
  
  File file;                                                                          //Create a file object
  
  /*if(!index)
  {
    Serial.println((String)"UploadStart: " + filename);
  }
  for(size_t i=0; i<len; i++)
  {
    Serial.write(data[i]);
  }
  if(final)
  {
    Serial.println((String)"\nUploadEnd: " + filename + "," + index+len);
  }*/

  if(!index)                                                                          //If it is first part of file then...
  {
    file = SPIFFS.open(tmp_filename, "w");                                            //Open the file for writing in SPIFFS (overwrite any available)
  }
  else
    file = SPIFFS.open(tmp_filename, "a+");                                           //Open the file for writing in SPIFFS (create if it doesn't exist)

  file.write(data, len);                                                              //Write the file
  
  if(file) 
  {                                                                                   // If the file was successfully created
    file.close();                                                                     //Close the file again     
    request->send(200);                                                               //Success code
  }
  else 
  {
    request->send(500, "text/plain", "500: couldn't create file");
  }
}
/**********************************************************************************************************************************************************************/

/************************************************************File Info Send Function***********************************************************************************/
void handle_finf(AsyncWebServerRequest *request)
{
  //Serial.println("Req = /getfinf");

  file_index = 1;                                         //Set File index to 1 on load
  
  String file_name[5], file_size[5], response = "";
  File temp;
  for(int i = 0; i < 5; i++)                              //Check all files in SPIFFS starting with "x(file number)_"
  {
    file_name[i] = file_search(i+1);                      //Save names in file_name array
    if(file_name[i] != '\0')
    {
      temp = SPIFFS.open(file_name[i]);
      file_size[i] = String(temp.size()) + " bytes";      //Save their size info in file_size array
    }
    else                                                  //If not available then append "----" to both arrays
    {
      file_name[i] = "----";
      file_size[i] = "----";
    }
  }
  
  for(int i = 0; i < 5; i++)                              //Add array info in a single string to send to client
  {
    response += file_name[i];
    response += "$";
    response += file_size[i];
    response += "$";
  }
  //Serial.println("response = " + response);
  request->send(200, "text/plain", response);             //Reply client with response
}
/*************************************************************************************************************************************************************************/

/*****************************************************************SSID Setup Function*************************************************************************************/
bool handle_ssid(AsyncWebServerRequest *request)
{
  String ssid = request->arg("ss");                             //Take Arguments from the request
  String pass = request->arg("pass"); 
  //Serial.println("Req = /ssid");
  //Serial.println("arg = " + ssid + " & " + pass);
  if (request->hasArg("ss") && request->hasArg("pass"))         
  {  
    sta_ssid = ssid.c_str();
    sta_pass = pass.c_str();
    //Serial.print(sta_ssid);
    //Serial.print(" + ");
    //Serial.print(sta_pass);
    WiFi.disconnect();                                          //Disconnect previously connected network
    //Serial.println("\nDisconnect OK");
    if(WiFi.begin(sta_ssid, sta_pass))                          //Connect to requested network
    {
      //delay(1000);
      //Serial.println("Connected" + WiFi.localIP());           
      request->send(200, "text/plain", "Connected");            //Reply with success if connected
    }
    else 
    {
      //Serial.println("Unable to Connect");
      request->send(500, "text/plain", "Unable to Connect");    //Else error
    }
  }
}
/*************************************************************************************************************************************************************************/

/**************************************************************Programming Enable Command*********************************************************************************/
void handle_checkconn(AsyncWebServerRequest *request)
{
  //Serial.println("Req = /checkconn");

  if(checked == 0)                                                      //If programming enable is not done previously
  { 
    byte recieved = 0;
    hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));   //Start spi transaction

    digitalWrite(27, HIGH);                                             //8051 needs reset high to program
    delay(1);
  
    hspi->transfer(0b10101100);                                         //Send programming Enable Command and recieve ack byte
    hspi->transfer(0b01010011);
    hspi->transfer(0b00000000);
    recieved = hspi->transfer(0b00000000);

    if(recieved == 0b01101001)                                          //If ack is received then success
    {
      checked = 1;
      request->send(200, "text/plain", "<span style = 'color:#00f700; font-weight:bold'>Connection Succesful</span>");
    }
    else                                                                //Else error
      request->send(200, "text/plain", "<span style = 'color:#ff1212; font-weight:bold'>Connection Unsuccessful</span>");
  }

  else if(checked == 1)                                                 //If programming enable done previously
  {
    request->send(200, "text/plain", "<span style = 'color:#00f700; font-weight:bold'>Connection Succesful</span>");
  }
}
/**************************************************************************************************************************************************************************/

/********************************************************Microcontroller Programming functions*****************************************************************************/
int strhex2int(char h, char l)                                                        //Program to convert String of hex to integer
{                                                                                      
    int val = 0; 
    //Transform hex character to the 4bit equivalent number, using the ascii table indexes
    if (l >= '0' && l <= '9')  l = l - '0';
    else if (l >= 'a' && l <='f') l = l - 'a' + 10;
    else if (l >= 'A' && l <='F') l = l - 'A' + 10;

    if (h >= '0' && h <= '9') h = h - '0';
    else if (h >= 'a' && h <='f') h = h - 'a' + 10;
    else if (h >= 'A' && h <='F') h = h - 'A' + 10;

    //Shift 4 to make space for new digit, and add the 4 bits of the new digit 
    val = (h << 4) | (l & 0xF);

    return val;
}

void chip_erase()
{                                                                                    //Erase Microcontroller Function
  hspi->transfer(0b10101100);                                                        //Send erase Command
  hspi->transfer(0b10000000);
  hspi->transfer(0);
  hspi->transfer(0);

  delay(500);                                                                        //Microcontroller needs atleast 500 ms to erase
}


void write_byte(int address, char dat)
{
  hspi->transfer(0b01000000);              //byte 1, write cmd
  hspi->transfer((address >> 8) & 0xFF);   //byte 2, address msb
  hspi->transfer(address & 0xFF);          //byte 3, address lsb
  hspi->transfer(dat);                     //byte 4, data write
  delay(1);                               //allow some time after each write cycle
}


char read_byte(int address)
{
  char ret=0;
  
  hspi->transfer(0b00100000);              //byte 1, read cmd
  hspi->transfer((address >> 8) & 0xFF);   //byte 2, address msb
  hspi->transfer(address & 0xFF);          //byte 3, address lsb
  ret = hspi->transfer(0);                 //byte 4, data read
  delay(1);

  return ret;
}


bool verify_byte(int address, char dat)
{
  if(dat == read_byte(address))
    return 1;
  else 
    return 0;
}


void handle_reading(AsyncWebServerRequest *request)
{
  mem_len = String(request->arg("mem")).toInt();          //How much memory user wants to read

  //Serial.println("Req = /read");
  wtype = 1;                                               // Request to read 
  request->send(SPIFFS, "/read.html", "text/html");        // Redirect the client to the read page
}


void handle_program(AsyncWebServerRequest *request)
{
  //Serial.println("Req = /program");
  wtype = 0;                                             // Request to program 
  request->send(SPIFFS, "/program.html", "text/html");   // Redirect the client to the program page
}


void handle_fprog(AsyncWebServerRequest *request)
{
  //Serial.println("Req = /fprog");
  wtype = 2;                                             // Request to program 
  request->send(SPIFFS, "/fprog.html", "text/html");   // Redirect the client to the program page
}


void handle_verification(AsyncWebServerRequest *request)
{
  //Serial.println("Req = /v_req");

  int record_length, address, dat;
  File file = SPIFFS.open(file_search(file_index), "r");    //Open required file 
        
  if(file)
  {  
    for(int i = 0; i < file.size(); i++)
    {
      if(file_read(file, i) == ':')
      {
        record_length = strhex2int(file_read(file, i+1),  file_read(file, i+2));
        address = (strhex2int(file_read(file, i+3),  file_read(file, i+4)) << 8) | (strhex2int(file_read(file, i+5),  file_read(file, i+6)) & 0xFF);

        for(int j = 0; j < (record_length*2); j += 2)                             //Read and verify data from microcontroller
        {
          dat = strhex2int(file_read(file, i+j+9),  file_read(file, i+j+10));

          if(!verify_byte(address, dat))
          {                                               
              request->send(200, "text/plain", "v_fail");                     //if any byte is mismatched then throw error
              file.close();                                                   //follow proper manners :-P ;-)
              return;
          }
          
          address++;
          //delay(1);
        }
        i += (2*record_length) + 8;
      }
      else
      {                                                                       //if file is not a valid intel hex file then throw error
        if((file_read(file, 0) != ':') && (file_read(file, 1) != ':') && (file_read(file, 2) != ':'))
          request->send(200, "text/plain", "v_fail");
        file.close();
        return;
      }
    }
    
    request->send(200, "text/plain", "v_success");
    file.close();
  }
}
/**************************************************************************************************************************************************************************/

/***********************************************************************Websocket Event Function***************************************************************************/
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)                         //Websocket for web server based programming
{
  //Serial.println("Req = WebSocketEvent");
  
  int record_length, address, dat, flag=0, retry_count = 0;
  
  if(type == WStype_CONNECTED)                  //If websocket is connected
  {
    if(wtype == 0)                              //If programming is requested
    {
      File file = SPIFFS.open(file_search(file_index), "r");    //Open required file 
          
      if(file)
      {
  
        chip_erase();             //Erase microcontroller before every programming cycle
  
        //hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));   //start spi transaction
         
        webSocket.broadcastTXT("Started Programming.....\n");
        
        for(int i = 0; i < file.size(); i++)
        {
          if(file_read(file, i) == ':')
          {
            record_length = strhex2int(file_read(file, i+1),  file_read(file, i+2));
            address = (strhex2int(file_read(file, i+3),  file_read(file, i+4)) << 8) | (strhex2int(file_read(file, i+5),  file_read(file, i+6)) & 0xFF);
  
            webSocket.broadcastTXT("\nrecord_length = " + String(record_length) + " address = " + String(address) + " : ");
            
            for(int j = 0; j < (record_length*2); j += 2)                             //Send data to microcontroller
            {
              dat = strhex2int(file_read(file, i+j+9),  file_read(file, i+j+10));
    
              write_byte(address, dat);             //Write the byte to 8051

              if(dat/10 == 0)          //Display formatting
                webSocket.broadcastTXT("  ");
              else if(dat/100 == 0)
                webSocket.broadcastTXT(" ");
  
              webSocket.broadcastTXT(String(dat) + " ");  //Send the written byte to websocket
  
              retry_count = 0;
              while(!verify_byte(address, dat))       //If the read byte does not match with send byte then
              {
                write_byte(address, dat);             //Write the byte again to 8051
                
                if(retry_count > 5)                   //Try for 5 times
                  goto jump_at_fail;                  //If still not done then jump to given location
  
                retry_count++;
              }  
              
              address++;
  
              //delay(1);       //Add some extra delay as stated in datasheet to write a data
            }
            i += (2*record_length) + 8;
            flag = 1;
          }
        }
      }
  
      file.close();
    }

    else if(wtype == 1)                         //If reading is requested
    {
        webSocket.broadcastTXT("Started Reading.....\n");
        
        for(int i = 0; i < mem_len; i++)
        {   
          dat = read_byte(i);             //Read byte from 8051

          if(dat/10 == 0)          //Display formatting
            webSocket.broadcastTXT("  ");
          else if(dat/100 == 0)
            webSocket.broadcastTXT(" ");
                
          webSocket.broadcastTXT(String(dat) + " ");  //Send the read byte to websocket

          if((i+1)%32 == 0)
            webSocket.broadcastTXT("\n");
          else if((i+1)%8 == 0)
            webSocket.broadcastTXT("  ");

          //delay(1);
          flag = 1;
        }

        webSocket.broadcastTXT("\nDone Reading.....\n");
    }

    else if(wtype == 2)
    {
      byte recieved = 0;
      hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));   //start spi transaction
  
      digitalWrite(27, HIGH);       //8051 needs reset high to program
      delay(1);
    
      hspi->transfer(0b10101100);     //Send programming Enable Command and recieve ack byte
      hspi->transfer(0b01010011);
      hspi->transfer(0b00000000);
      recieved = hspi->transfer(0b00000000);

      if(recieved == 0b01101001)
      {
        File file = SPIFFS.open("/temp.hex", "r");    //Open required file 

        if(file)
        {
          chip_erase();             //Erase microcontroller before every programming cycle
  
          webSocket.broadcastTXT("Started Programming.....\n");
        
          for(int i = 0; i < file.size(); i++)
          {
            if(file_read(file, i) == ':')
            {
              record_length = strhex2int(file_read(file, i+1),  file_read(file, i+2));
              address = (strhex2int(file_read(file, i+3),  file_read(file, i+4)) << 8) | (strhex2int(file_read(file, i+5),  file_read(file, i+6)) & 0xFF);
  
              webSocket.broadcastTXT("\nrecord_length = " + String(record_length) + " address = " + String(address) + " : ");
            
              for(int j = 0; j < (record_length*2); j += 2)                             //Send data to microcontroller
              {
                dat = strhex2int(file_read(file, i+j+9),  file_read(file, i+j+10));
    
                write_byte(address, dat);             //Write the byte to 8051

                if(dat/10 == 0)                       //Display formatting
                  webSocket.broadcastTXT("  ");
                else if(dat/100 == 0)
                  webSocket.broadcastTXT(" ");
  
                webSocket.broadcastTXT(String(dat) + " ");  //Send the written byte to websocket
  
                retry_count = 0;
                while(!verify_byte(address, dat))       //If the read byte does not match with send byte then
                {
                  write_byte(address, dat);             //Write the byte again to 8051
                
                  if(retry_count > 5)                   //Try for 5 times
                    goto jump_at_fail;                  //If still not done then jump to given location
  
                  retry_count++;
                }  
              
                address++;
              }
              i += (2*record_length) + 8;
              flag = 1;
            }
          
            else
            {                                                                       //if file is not a valid intel hex file then throw error
              if((file_read(file, 0) != ':') && (file_read(file, 1) != ':') && (file_read(file, 2) != ':'))
               webSocket.broadcastTXT("INVALID FILE!!!");
            }
          }
        }
  
        file.close();
      }
      
      else
        webSocket.broadcastTXT("Unable to communicate with Device...");
    }

    else
      __asm__("nop\n\t");
      //Serial.println("Unable to process request");
  }

jump_at_fail:
  hspi->endTransaction();   //end transaction
  digitalWrite(27, LOW);
  checked = 0;

  webSocket.broadcastTXT("\n");
 
  if(flag == 1)
    webSocket.broadcastTXT("success");
  else if(flag == 0)
    webSocket.broadcastTXT("failed");

  webSocket.disconnect();
}
/**************************************************************************************************************************************************************************/

/***********************************************************************Websocket 2 Event Function*************************************************************************/
void webSocket2Event(uint8_t num, WStype_t type, uint8_t * payload, size_t length)                        //Websocket 2 for software based programming
{
  char * WStype[] PROGMEM = {"WStype_ERROR", "WStype_DISCONNECTED", "WStype_CONNECTED",
                             "WStype_TEXT", "WStype_BIN", "WStype_FRAGMENT_TEXT_START",
                             "WStype_FRAGMENT_BIN_START", "WStype_FRAGMENT", "WStype_FRAGMENT_FIN",
                             "WStype_PING", "WStype_PONG"
                            };
  
  Serial.println("Req = WebSocket2Event, " + String(WStype[type]) + ", ID = " + String(num));
  
  if(type == WStype_CONNECTED)                  //If websocket 2 is connected
  {
    byte recieved = 0;
    hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));   //start spi transaction

    digitalWrite(27, HIGH);       //8051 needs reset high to program
    delay(1);
  
    hspi->transfer(0b10101100);     //Send programming Enable Command and recieve ack byte
    hspi->transfer(0b01010011);
    hspi->transfer(0b00000000);
    recieved = hspi->transfer(0b00000000);

    if(recieved == 0b01101001)
      webSocket2.broadcastTXT("conn");
    else
    {
      webSocket2.broadcastTXT("error_conn");
      //webSocket2.disconnect();
    }
  }

  if(type == WStype_TEXT)                  //If websocket 2 sends data
  {
    //for(int i = 0; i < length; i++)
    //  Serial.print((char) payload[i]); 
    //Serial.println();
    //webSocket2.broadcastTXT("server message");

    if(String((char*)payload).substring(0) == "erase")                  //If Erase command is received
    {
      chip_erase();
      webSocket2.broadcastTXT("done");
    }
    else if(((char)payload[0] == 'w') && ((char)payload[1] == ':'))   //if byte to write is received
    {
      int sepr = String((char*)payload).lastIndexOf(':');         //Find separating ':'
      int dat = -1, addr = -1;
      dat = (String((char*)payload).substring(2, sepr)).toInt();      //Find data in string 
      addr = (String((char*)payload).substring(sepr+1)).toInt();      //Find address in string
      write_byte(addr, dat);                       //Write to microcontroller
      webSocket2.broadcastTXT("ok");               //Reply with ok
    }
    else if(((char)payload[0] == 'r') && ((char)payload[1] == ':'))   //if read is requested
    {
      int sepr = String((char*)payload).lastIndexOf(':');         //Find seprating ':'
      int addr = (String((char*)payload).substring(sepr+1)).toInt();  //Find address in string
      String x = String((int)read_byte(addr));                     //Read the address 
      webSocket2.broadcastTXT(x);                  //Reply with data
    }
    else 
      webSocket2.broadcastTXT("unknown command");
  }

  if(type == WStype_DISCONNECTED)                  //If websocket 2 is disconnected
  {
    hspi->endTransaction();   //end transaction
    digitalWrite(27, LOW);
  }
}
/**************************************************************************************************************************************************************************/

/*************************************************************************Local File Functions*****************************************************************************/
char file_read(File file, int pos)
{
  file.seek(pos);
  return file.read();
}


String file_search(int i)
{
  File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file)
    {
      if( (file.name())[1] == 'x' && (file.name())[2] == (i+48) &&  (file.name())[3] == '_' )
        return file.name();
      file = root.openNextFile();
    }

  return String('\0');
}


bool file_delete(String path) 
{
  if(path != '\0')
  {
      SPIFFS.remove(path);
      return 1;
    }
    return 0;
}
/**************************************************************************************************************************************************************************/
