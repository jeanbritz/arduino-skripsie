#include <SPI.h>
#include <PN532.h>
#include <SoftwareSerial.h>

#include <avr/power.h>
#include <avr/sleep.h>

#include <NdefMessage.h>
#include <NFCLinkLayer.h>
#include <SNEP.h>

// GSM definitions
#define unoTXPin (7) // Uno -> GSM
#define unoRXPin (6) // Uno <- GSM
#define powerGSMPin (8)
#define gsmBaudRate (9600)
#define unoBaudRate (115200)

// AT command related
#define AT_CREG_QUERY ("AT+CREG?")
#define AT_CHTTPACT_COMMAND ("AT+CHTTPACT=\"ml.sun.ac.za\",8000")
#define AT_CHTTPACT_INIT_RESPONSE ("+CHTTPACT: REQUEST")
#define AT_CHTTPACT_DATA_RESPONSE ("+CHTTPACT: DATA,")
#define AT_CHTTPACT_DONE_RESPONSE ("+CHTTPACT:0")

#define HTTP_INVOICE_POST_CONST ("POST /arduino/invoice/add HTTP/1.1\r\nHost:ml.sun.ac.za:8000\r\nContent-Type:application/x-www-form-urlencoded\r\n")
#define HTTP_PAYMENT_POST_CONST ("POST /arduino/payment HTTP/1.1\r\nHost:ml.sun.ac.za:8000\r\nContent-Type:application/x-www-form-urlencoded\r\n")
#define HTTP_CONTENT_LENGTH ("Content-Length:%d\r\n\r\n")
#define HTTP_INVOICE_POST_PARAMS ("user=%d&amount_payable=%d\r\n\r\n") // parameters sent in Content body e.g. user=<id>&amount_payable=<int> 
#define HTTP_PAYMENT_POST_PARAMS ("user=%d&invoice_id=%d&amount=%d\r\n\r\n") // parameters sent in Content body e.g. user=<id>&invoice_id=<int>&amount=<int> 


// special chars/strings
#define EOS ('\0')
#define ESC (0x1A)
#define CR  (0x0D)
#define LF  (0x0A)
#define OK  ("OK")

// NFC Definitions
#define nfcSS (10)
// This message shall be used to rx or tx 
// NDEF messages it shall never be released
#define MAX_PKT_HEADER_SIZE  (50)
#define MAX_PKT_PAYLOAD_SIZE (34)
uint8_t rxNDEFMessage[MAX_PKT_HEADER_SIZE + MAX_PKT_PAYLOAD_SIZE];
uint8_t *rxNDEFMessagePtr;
char rxNDEFRecord [MAX_PKT_PAYLOAD_SIZE];
uint8_t *rxNDEFRecordPtr;


PN532 nfc(nfcSS);
NFCLinkLayer linkLayer(&nfc);
SNEP snep(&linkLayer);
SoftwareSerial gsmSerial(unoRXPin, unoTXPin);

#define statusLED (A2)
#define CLAIM_REQUEST ("GET /arduino/claim?user=%d&id=%d HTTP/1.1\r\nHost:ml.sun.ac.za:8000\r\nContent-Length:0\r\n\r\n")

char auxString [200];
char *ptrAuxString = 0;
char httpResponse = 0;

int8_t ptrDummy = 0;
int8_t nfcFailureCounter = 0;
int8_t answer = 0;
boolean success = 0;


int option = 0;
char inputClaimId[3];
char inputUserId [3];
char inputInvoiceId[3];
char inputAmount[5];

int user_id_int = 0;
int claim_id_int = 0;
int invoice_id_int = 0;
int amount_payable = 0;
int amount_int = 0;

void setup() {

  pinMode(statusLED, OUTPUT);  
  digitalWrite(statusLED, HIGH);
  Serial.begin(unoBaudRate);
  gsmSerial.begin(gsmBaudRate);

  gsm_power_on();
  while( (sendATcommand(AT_CREG_QUERY, "+CREG: 0,1", 500) || sendATcommand(AT_CREG_QUERY, "+CREG: 0,5", 500)) == 0 );
  Serial.println(F("GSM : Registered to network"));


  nfc.initializeReader();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print(F("NFC : Didn't find PN53x board"));
    while (1); // halt
  }
  Serial.println(F("NFC : Peer-to-Peer Mode activated"));

  nfc.SAMConfig();
  Serial.print(F("============================\n"));
  Serial.print(F("=          Ready!          =\n"));
  Serial.print(F("============================\n"));
}

void loop() {

  while(1) {
    Serial.print(F("=======================================\n"));
    Serial.print(F("=    NFC PAYMENT CONTROLLER MODULE    =\n"));
    Serial.print(F("=======================================\n"));
    Serial.print(F("=             MAIN MENU               =\n"));
    Serial.print(F("=======================================\n"));
    Serial.println(F("1) Create an invoice"));
    Serial.println(F("2) Listen to NFC interaction"));

    while(1) {
      while(!Serial.available());
      option = serialReadInt();
      if(option > 0) break;
    }

    if(option == 1)
    {
      option = 0;
       Serial.print(F("=======================================\n"));
       Serial.print(F("=           CREATE INVOICE            =\n"));
       Serial.print(F("=======================================\n"));
      Serial.print (F("Enter user's id number:"));
  
  while(1) {
      while(!Serial.available());
      user_id_int = serialReadInt ();
     // user_id = user_id | (char)(Serial.read()<<4);
    // delay(15);
     // user_id = user_id | (char)(Serial.read());
      if(user_id_int > 0) break;
   }
   Serial.println(user_id_int);
   Serial.print (F("Enter the amount the user have to pay:"));
   
   while(1) {
      while(!Serial.available());
      amount_payable = serialReadInt();
      if(amount_payable > 0) break;
   }
     Serial.println(amount_payable); 
     
     makeInvoiceHttpRequest(user_id_int, amount_payable);
    }
    else if (option == 2)
    {
       Serial.print(F("=======================================\n"));
       Serial.print(F("=    WATING FOR NFC INTERACTION       =\n"));
       Serial.print(F("=======================================\n"));
      option = 0;

      uint32_t rxResult = GEN_ERROR; 
      while(success)
      {
        Serial.print(F("============================\n"));
        Serial.print(F("=          Ready!          =\n"));
        Serial.print(F("============================\n"));
        success = false;
      }
#ifdef ENABLE_SLEEP
      if (IS_ERROR(nfc.configurePeerAsTarget(SNEP_SERVER))) {
        //sleepMCU();

        extern uint8_t pn532_packetbuffer[];
        nfc.readspicommand(PN532_TGINITASTARGET, (PN532_CMD_RESPONSE *)pn532_packetbuffer);
      }
#else
      if (IS_ERROR(nfc.configurePeerAsTarget(SNEP_SERVER))) {
        extern uint8_t pn532_packetbuffer[];

        while (!nfc.isReady()) {
        }
        nfc.readspicommand(PN532_TGINITASTARGET, (PN532_CMD_RESPONSE *)pn532_packetbuffer);
      }
#endif
      while(success == false) {
        rxNDEFMessagePtr = &rxNDEFMessage[0];
        ptrAuxString = &auxString[0];
        httpResponse = 0;
        rxResult = snep.rxNDEFPayload(rxNDEFMessagePtr);


        if (rxResult == SEND_COMMAND_RX_TIMEOUT_ERROR)  {
          break;
        } 
        else if (IS_ERROR(rxResult)) {
          if(nfcFailureCounter > 10) {
            nfcFailureCounter = 0;
            Serial.println(F("Tip: Try to adjust the orientation of your device towards the NFC-interface"));
          }
          else
          {
            nfcFailureCounter++;
          }


        }

        if (RESULT_OK(rxResult)) {
          NdefMessage *message = new NdefMessage(rxNDEFMessagePtr, rxResult);
          blinkStatusLED();
          NdefRecord record = message->getRecord(0);
          rxNDEFRecordPtr = record.getPayload(); // reads the payload

            // transfer payload into char array
          int8_t counter = 0;
          while(*(rxNDEFRecordPtr) != '\0') {
            char c = (char)*(rxNDEFRecordPtr++);
            rxNDEFRecord[counter++] = c;
          }

          Serial.print(F("Raw NDEF Record : ")); 
          Serial.println(rxNDEFRecord);
          
          Serial.print(F("User ID: ")); 
          
          inputUserId[0] = rxNDEFRecord[1];
          inputUserId[1] = rxNDEFRecord[2];
          inputUserId[2] = '\0';
          user_id_int = atoi(inputUserId);
          Serial.print(user_id_int);
          if(rxNDEFRecord[0] != 'P')
          {
            inputClaimId[0] = rxNDEFRecord[4];
            inputClaimId[1] = rxNDEFRecord[5];
            inputClaimId[2] = '\0';
            claim_id_int = atoi(inputClaimId);
            Serial.print(F(" | Claim ID: ")); 
            Serial.println(claim_id_int);
          }
          else
          {
            inputInvoiceId[0] = rxNDEFRecord[4];
            inputInvoiceId[1] = rxNDEFRecord[5];
            inputInvoiceId[2] = '\0';
            invoice_id_int = atoi(inputInvoiceId);
            inputAmount[0] = rxNDEFRecord[7];
            inputAmount[1] = rxNDEFRecord[8];
            inputAmount[2] = rxNDEFRecord[9];
            inputAmount[3] = rxNDEFRecord[10];
            inputAmount[4] = '\0';
            amount_int = atoi(inputAmount);
            Serial.print(F(" | Invoice ID: ")); 
            Serial.println(invoice_id_int);
            Serial.print(F("Amount that user want to pay: "));
            Serial.println(amount_int); 
          }
          
          
          if(rxNDEFRecord[0] == 'V')
          {

            Serial.println(F("Process started for claiming the voucher"));
            httpResponse = doClaimHttpRequest(user_id_int, claim_id_int);
          }
          else if(rxNDEFRecord[0] == 'T')
          {
            Serial.println(F("Process started for claiming the ticket"));
            httpResponse = doClaimHttpRequest(user_id_int, claim_id_int);
          }
          else if(rxNDEFRecord[0] == 'P')
          {
            Serial.println(F("Process started to make a payment"));
            httpResponse = makePaymentHttpRequest(user_id_int, invoice_id_int, amount_int);
          }
          else
          {
            Serial.println(F("Unknown NDEF Message"));
          }


          if(httpResponse == 'E')
          {
            if(rxNDEFRecord[0] != 'P') {
              Serial.println(F("The voucher has expired!"));
              success = true;
            }
            else
            {
              Serial.println(F("The payment process is completed"));
            }
          }
          else if (httpResponse == 'V')
          {
            if(rxNDEFRecord[0] != 'P') {
            Serial.println(F("The voucher is valid!"));
            success = true;
            }
            else
            {
               Serial.println(F("The payment was accepted"));
            }
          }
          else if (httpResponse == 'I')
          {
            Serial.println(F("User has insufficient funds"));
          }
          else
          {
            Serial.println(F("Error : Unknown response"));
            success = true;
          }

          Serial.print("Memory Free : ");Serial.println(memoryFree());

          //free(rxNDEFRecord);
          // free(message);

        }

      }

    }
    //free(rxNDEFRecord);
    Serial.print(F("Memory Free : "));
    Serial.println(memoryFree());

  }

}

// variables created by the build process when compiling the sketch
extern int __bss_end;
extern void *__brkval;
// function to return the amount of free RAM
int memoryFree(){
  int freeValue;
  if((int)__brkval == 0) 
    freeValue = ((int)&freeValue) - ((int)&__bss_end);
  else 
    freeValue = ((int)&freeValue) - ((int)__brkval);
  return freeValue;
}

void gsm_power_on(){

  uint8_t answer=0;

  // checks if the module is started
  answer = sendATcommand("AT", "OK", 2000);
  if (answer == 0)
  {
    // power on pulse
    digitalWrite(powerGSMPin,HIGH);
    delay(200);
    digitalWrite(powerGSMPin,LOW);
    delay(6000);
    // waits for an answer from the module
    while(answer == 0){    
      // Send AT every two seconds and wait for the answer
      answer = sendATcommand("AT", OK, 2000);    
    }
  }

}

int8_t sendATcommand(char* ATcommand, char* expected_answer1,
unsigned int timeout)
{

  uint8_t x=0,  answer=0;
  char response[100];
  unsigned long previous;

  memset(response, EOS, 100);    // Initialize the string

  delay(100);

  while( gsmSerial.available() > 0) gsmSerial.read();    // Clean the input buffer

  gsmSerial.println(ATcommand);    // Send the AT command 


    x = 0;
  previous = millis();

  // this loop waits for the answer
  do{

    if(gsmSerial.available() != 0){    
      response[x] = gsmSerial.read();
      x++;
      // check if the desired answer is in the response of the module
      if (strstr(response, expected_answer1) != NULL)    
      {
        answer = 1;
      }
    }
    // Waits for the asnwer with time out
  }
  while((answer == 0) && ((millis() - previous) < timeout));    

  return answer;
}

void blinkStatusLED ()
{
  // Status LED is active on LOW
  PINC = bit (2); // toggle A2 
  delay(300);              
  PINC = bit (2); // toggle A2 
  delay(300);               
  PINC = bit (2); // toggle A2
  delay(100);
  PINC = bit (2); // toggle A2

}

int serialReadInt()
{
  int i, serialAvailable;                       
  char inputBytes [7];                
  char * inputBytesPtr = &inputBytes[0];  

  if (Serial.available()>0)          
  {
    delay(5);                            
    // 5mS work great for 9600 baud (increase this number for slower baud)
    serialAvailable = Serial.available();  // Read number of input bytes
    for ( i= 0; i < serialAvailable; i++)       // Load input bytes into array
      if(i < (serialAvailable - 1)){
        inputBytes[i] = (char)Serial.read();
      }
      else {
        inputBytes[i] =  '\0';             // Put NULL character at the end
      }
    return atoi(inputBytesPtr);    // Call atoi function and return result
  }
  else
    return -1;                           // Return -1 if there is no input
}


void printClaim(char type)
{
  Serial.print("\n");
  Serial.println(F("\\    \\    \\    /    /     /"));
  Serial.println(F(" \\    \\    \\  /    /     / "));
  Serial.println(F("  \\    \\    \\/    /     /  "));
  Serial.println(F("   \\    \\         /     /   "));
  Serial.println(F("    \\    \\       /     /    "));
  Serial.println(F("\\   \\    \\     /     /    /"));
  Serial.println(F(" \\   \\    \\   /     /    / "));
  Serial.println(F("  \\   \\    \\ /     /    /  "));
  Serial.println();
  if(type == 'V')
  {
    Serial.print(F(" _______________________________"));
    Serial.print(F("| ============================= |\n"));
    Serial.print(F("| =          VOUCHER         =  |\n"));
    Serial.print(F("| ============================= |\n"));
    Serial.println();
  }
  else
  {
    Serial.print(F(" _______________________________"));
    Serial.print(F("| ============================= |\n"));
    Serial.print(F("| =         TICKET            = |\n"));
    Serial.print(F("| ============================= |\n"));
  }
    Serial.print(F("| Claimed by: User ID "));Serial.println(user_id_int);    
    Serial.print(F("Title : "));
    for(int8_t i = 7; i < 17; i++) {
      Serial.print(rxNDEFRecord[i]);
      
    }
    Serial.println();
    Serial.print (F("Valid until : "));
    
    Serial.print(rxNDEFRecord[26]);Serial.print(rxNDEFRecord[27]);
    Serial.print(F("/")); 
     Serial.print(rxNDEFRecord[23]);Serial.print(rxNDEFRecord[24]);
    Serial.print(F("/")); 
    Serial.print(rxNDEFRecord[18]);Serial.print(rxNDEFRecord[19]);Serial.print(rxNDEFRecord[20]);Serial.println(rxNDEFRecord[21]);
    Serial.print(F("Amount : "));Serial.print(rxNDEFRecord[29]);Serial.print(rxNDEFRecord[30]);Serial.print(rxNDEFRecord[31]);Serial.println(rxNDEFRecord[32]);
    Serial.print(F("============================\n"));
    Serial.print(F("=       THANK YOU          =\n"));
    Serial.print(F("============================\n"));
}

char doClaimHttpRequest(int user_id, int claim_id)
{

  int8_t resultAT = 0;
  char response = 0;
  int aux = 0;
  int data_size = 0;

  Serial.print(F("============================\n"));
  Serial.print(F("=  Start HTTP Transaction  =\n"));
  Serial.print(F("============================\n"));

  resultAT = sendATcommand(AT_CHTTPACT_COMMAND, AT_CHTTPACT_INIT_RESPONSE, 60000);

  Serial.println(AT_CHTTPACT_INIT_RESPONSE);

  if (resultAT == 1)
  {  
    free(auxString);
    Serial.println(F("Sends Request:"));
    //memset(ptrAuxString, EOS, 200);    // Initialize the string
    

    sprintf(auxString, CLAIM_REQUEST, user_id, claim_id);
    Serial.println(auxString);    
    gsmSerial.println(auxString);
    // Sends 
    auxString[0] = ESC;
    auxString[1] = 0x00;
    answer = sendATcommand(auxString, AT_CHTTPACT_DATA_RESPONSE, 60000);

    if(resultAT == 1)
    {
      Serial.println(AT_CHTTPACT_DATA_RESPONSE);

      while(gsmSerial.available() == 0);
      aux = gsmSerial.read();
      do{
        data_size *= 10;
        data_size += (aux-0x30);
        while(gsmSerial.available() == 0);
        aux = gsmSerial.read();        
      }
      while(aux != CR);

      while(gsmSerial.available() < (_SS_MAX_RX_BUFF - 1));
      while(data_size > 1)
      {
        while(gsmSerial.available() == 0);
        ptrDummy =  gsmSerial.read();  
        data_size--;
      }

      response = (char)gsmSerial.read();
      Serial.print(F("Response: "));
      Serial.println(response);


    }

    Serial.println();
    Serial.print(F("============================\n"));
    Serial.print(F("=   Ends HTTP Transaction  =\n"));
    Serial.print(F("============================\n"));

  }
  return response;
}

char makeInvoiceHttpRequest(int user_id, int amount_payable)
{

  int8_t resultAT = 0;
  char response = 0;
 
  int aux = 0;
  int data_size = 0;

  Serial.print(F("============================\n"));
  Serial.print(F("=  Start HTTP Transaction  =\n"));
  Serial.print(F("============================\n"));

  resultAT = sendATcommand(AT_CHTTPACT_COMMAND, AT_CHTTPACT_INIT_RESPONSE, 60000);

  Serial.println(AT_CHTTPACT_INIT_RESPONSE);

  if (resultAT == 1)
  {  
    free(auxString);
    Serial.println(F("Sends Request:"));
      Serial.print(HTTP_INVOICE_POST_CONST);
      gsmSerial.print(HTTP_INVOICE_POST_CONST);
      sprintf(auxString, HTTP_INVOICE_POST_PARAMS, user_id, amount_payable);
      int8_t content_length = get_array_length(auxString);
      sprintf(auxString, HTTP_CONTENT_LENGTH, content_length);
      Serial.print(auxString);
      gsmSerial.print(auxString);
      sprintf(auxString, HTTP_INVOICE_POST_PARAMS, user_id, amount_payable);
      Serial.print(auxString);
      gsmSerial.print(auxString);
      
    auxString[0] = ESC;
    auxString[1] = 0x00;
    answer = sendATcommand(auxString, AT_CHTTPACT_DATA_RESPONSE, 60000);

    if(resultAT == 1)
    {
      Serial.println(AT_CHTTPACT_DATA_RESPONSE);

      while(gsmSerial.available() == 0);
      aux = gsmSerial.read();
      do{
        data_size *= 10;
        data_size += (aux-0x30);
        while(gsmSerial.available() == 0);
        aux = gsmSerial.read();        
      }
      while(aux != CR);

      while(gsmSerial.available() < (_SS_MAX_RX_BUFF - 1));
      while(data_size > 1)
      {
        while(gsmSerial.available() == 0);
        ptrDummy =  gsmSerial.read();  
        data_size--;
      }

      response = (char)gsmSerial.read();
      Serial.print(F("Response: "));
      Serial.println(response);


    }

    Serial.println();
    Serial.print(F("============================\n"));
    Serial.print(F("=   Ends HTTP Transaction  =\n"));
    Serial.print(F("============================\n"));

  }
  return response;
}

char makePaymentHttpRequest(int user_id, int invoice_id, int amount)
{

  int8_t resultAT = 0;
  char response = 0;
 
  int aux = 0;
  int data_size = 0;

  Serial.print(F("============================\n"));
  Serial.print(F("=  Start HTTP Transaction  =\n"));
  Serial.print(F("============================\n"));

  resultAT = sendATcommand(AT_CHTTPACT_COMMAND, AT_CHTTPACT_INIT_RESPONSE, 60000);

  Serial.println(AT_CHTTPACT_INIT_RESPONSE);

  if (resultAT == 1)
  {  
    free(auxString);
    Serial.println(F("Sends Request:"));
      Serial.print(HTTP_PAYMENT_POST_CONST);
      gsmSerial.print(HTTP_PAYMENT_POST_CONST);
      sprintf(auxString, HTTP_PAYMENT_POST_PARAMS, user_id, invoice_id, amount);
      int8_t content_length = get_array_length(auxString);
      sprintf(auxString, HTTP_CONTENT_LENGTH, content_length);
      Serial.print(auxString);
      gsmSerial.print(auxString);
      sprintf(auxString, HTTP_PAYMENT_POST_PARAMS, user_id, invoice_id, amount);
      Serial.print(auxString);
      gsmSerial.print(auxString);
      
    auxString[0] = ESC;
    auxString[1] = 0x00;
    answer = sendATcommand(auxString, AT_CHTTPACT_DATA_RESPONSE, 60000);

    if(resultAT == 1)
    {
      Serial.println(AT_CHTTPACT_DATA_RESPONSE);

      while(gsmSerial.available() == 0);
      aux = gsmSerial.read();
      do{
        data_size *= 10;
        data_size += (aux-0x30);
        while(gsmSerial.available() == 0);
        aux = gsmSerial.read();        
      }
      while(aux != CR);

      while(gsmSerial.available() < (_SS_MAX_RX_BUFF - 1));
      while(data_size > 1)
      {
        while(gsmSerial.available() == 0);
        ptrDummy =  gsmSerial.read();  
        data_size--;
      }

      response = (char)gsmSerial.read();
      Serial.print(F("Response: "));
      Serial.println(response);


    }

    Serial.println();
    Serial.print(F("============================\n"));
    Serial.print(F("=   Ends HTTP Transaction  =\n"));
    Serial.print(F("============================\n"));

  }
  return response;
}

int8_t get_array_length(char *arr)
{
	int8_t k = 0;
	char *seek = arr;
	while('\0' != *seek++)
	{
		k++;
	}
	return k;
}








