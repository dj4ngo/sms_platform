#include <DHT.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>


/*
 * Need to increase buffer os SoftwareSerial
 * buffer increased in : 
 * /usr/share/arduino/hardware/arduino/avr/libraries/SoftwareSerial/src/SoftwareSerial.h
 * increased _SS_MAX_RX_BUFF from 64 to 250
 */

#define PIN_SIM800_RST 12
#define PIN_LED 13
#define PIN_HEATER 8
#define PIN_PLUG 9
#define PIN_DHT_INT 7
#define PIN_DHT_EXT 6

#define DHT_TYPE DHT22

/* Useful Constants */
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24L)

/* Useful Macros for getting elapsed time */
#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)  
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN)
#define numberOfHours(_time_) (( _time_% SECS_PER_DAY) / SECS_PER_HOUR)
#define elapsedDays(_time_) ( _time_ / SECS_PER_DAY)  

/* checks intervals */
#define checkalive_interval (600UL)
#define dht22_interval (60UL)

/* phone book */
#define PHONEBOOK_SIZE 10
// just used at first time to add the first admin
#define INIT_PHONEBOOK false
#define INIT_NAME "XXXX"
#define INIT_NUMBER "06XXXXXXXX"

/* timers */
unsigned long checkalive_last = 0;
unsigned long last_ok_received = 0;
unsigned long dht22_last = 0;


/* temperature / humidity */
SoftwareSerial sim800(10, 11);
DHT dht_int(PIN_DHT_INT, DHT_TYPE); 
DHT dht_ext(PIN_DHT_EXT, DHT_TYPE); 

float hum_int, temp_int, hum_ext, temp_ext;
float temp_int_min, temp_int_max, temp_ext_min, temp_ext_max;
bool temp_int_min_reached = false;
bool temp_int_max_reached = false;
bool temp_ext_min_reached = false;
bool temp_ext_max_reached = false;

/* String buffer for the GPRS shield message */
String msg = String("");
String from = String("");
String sms_position = String("");

/* thresholds */
short temp_int_high, temp_int_low, temp_ext_high, temp_ext_low;


/* Set to 1 when the next GPRS shield message will contains the SMS message */
bool SmsContentFlag = 0;

/* EEPROM addresses */
int temp_int_low_address = 0;
int temp_int_high_address = temp_int_low_address + sizeof(temp_int_low);
int temp_ext_low_address = temp_int_high_address + sizeof(temp_int_high);
int temp_ext_high_address = temp_ext_low_address + sizeof(temp_ext_low);
int phonebook_address = temp_ext_high_address + sizeof(temp_ext_high);

/* Phonebook */
struct Contact {
  char number[11] = "";
  char name[11] = "";
  bool admin = false;
};
static const struct Contact EmptyContact;
struct Contact phonebook[PHONEBOOK_SIZE];

/*---------------SETUP--------------*/
void setup()
{
  
  Serial.begin(9600);

  Serial.println(F("Arduino Setup..."));

  // Get EEPROM variables
  EEPROM.get(temp_int_low_address, temp_int_low);
  EEPROM.get(temp_int_high_address, temp_int_high);
  EEPROM.get(temp_ext_low_address, temp_ext_low);
  EEPROM.get(temp_ext_high_address, temp_ext_high);
  EEPROM.get(phonebook_address, phonebook);


   if(INIT_PHONEBOOK){
    String(INIT_NAME).toCharArray(phonebook[0].name, 11);
    String(INIT_NUMBER).toCharArray(phonebook[0].number, 11);
    phonebook[0].admin = true;
    for (int i=1; i < PHONEBOOK_SIZE; i++){
      phonebook[i] = EmptyContact;
    }
    EEPROM.put(phonebook_address, phonebook);

   }

  
  // temp sensor
  dht_int.begin();
  dht_ext.begin();
  // read initial values
  hum_int = dht_int.readHumidity();
  temp_int =  dht_int.readTemperature();
  temp_int_min = temp_int;
  temp_int_max = temp_int;
  hum_ext = dht_ext.readHumidity();
  temp_ext = dht_ext.readTemperature();
  temp_ext_min = temp_ext;
  temp_ext_max = temp_ext;

  pinMode(PIN_SIM800_RST, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_HEATER, OUTPUT);
  digitalWrite( PIN_HEATER, HIGH );
  pinMode(PIN_PLUG, OUTPUT);
  digitalWrite( PIN_PLUG, HIGH );
  
  Serial.println(F("Power on sim800"));
  digitalWrite(PIN_SIM800_RST, HIGH);
  delay(1000);

  sim800.begin(9600); 
  while (!sim800) {
    ; // wait for serial port to connect.
  }

  restart_sim800('n');

  Serial.println(F("Arduino Setup finished !"));

}

/*---------------LOOP--------------*/
void loop()
{
    while (Serial.available())
    {
      sim800.write(Serial.read());
     }  
    while (sim800.available())
    {
      char SerialInByte;
      SerialInByte = (unsigned char)sim800.read();

      
      Serial.print( String(SerialInByte) );

        // Check last byte to  If the message ends with <CR> then process the message
        if( SerialInByte == 13 ){
          ProcessGprsMsg();
         }
         if( SerialInByte == 10 ){
            // Skip Line feed
         }
         else {
           // store the current character in the message string buffer
           msg += String(SerialInByte);
         }
     }

    // DHT22 mesure temp and humidity
    if ( (unsigned long)(millis() - dht22_last) >= (unsigned long)(dht22_interval * 1000) ) {
      dht22_last = millis();

      hum_int = dht_int.readHumidity();  //Stores humidity value 
      temp_int = dht_int.readTemperature(); //Stores temperature value
      if (temp_int > temp_int_max) {
        temp_int_max = temp_int;
      }
      if (temp_int < temp_int_min) {
        temp_int_min = temp_int; 
      }

      hum_ext = dht_ext.readHumidity();  //Stores humidity value 
      temp_ext = dht_ext.readTemperature(); //Stores temperature value
      if (temp_ext > temp_ext_max) {
        temp_ext_max = temp_ext;
      }
      if (temp_ext < temp_ext_min) {
        temp_ext_min = temp_ext; 
      }

      // ----- check alerts
      if ( temp_int_min_reached &&  temp_int > temp_int_min +5){
        temp_int_min_reached = false;
      }
      if ( !temp_int_min_reached && temp_int < temp_int_min) {
        temp_int_min_reached = true;
        send_sms_to_admins(String("") + F("Alerte temp int min : ") + temp_int);
      }

      if ( temp_int_max_reached &&  temp_int < temp_int_max -5){
        temp_int_max_reached = false;
      }
      if ( !temp_int_max_reached && temp_int > temp_int_max) {
        temp_int_max_reached = true;
        send_sms_to_admins(String("") + F("Alerte temp int max : ") + temp_int);
      }

      if ( temp_ext_min_reached &&  temp_ext > temp_ext_min +5){
        temp_ext_min_reached = false;
      }
      if ( !temp_ext_min_reached && temp_ext < temp_ext_min) {
        temp_ext_min_reached = true;
        send_sms_to_admins(String("") + F("Alerte temp ext min : ") + temp_ext);
      }

      if ( temp_ext_max_reached &&  temp_ext < temp_ext_max -5){
        temp_ext_max_reached = false;
      }
      if ( !temp_ext_max_reached && temp_ext > temp_ext_max) {
        temp_ext_max_reached = true;
        send_sms_to_admins(String("") + F("Alerte temp ext max : ") + temp_ext);
      }

    
    }


    // Send a check alive (AT command)
    if ( (unsigned long)(millis() - checkalive_last) >= (unsigned long)(checkalive_interval * 1000) && !SmsContentFlag ) {
       checkalive_last = millis();
       
       Serial.println(F("Send AT checkalive"));
       
       sim800.println(F("AT"));
    }

    // Check latest OK has been received since less than 2 checkalive_interval
    if ( (unsigned long)(millis() - last_ok_received) >= ((unsigned long)(checkalive_interval) * 2 *1000) && !SmsContentFlag) {
       
      Serial.print(F("Error: no communication seen since more than "));//
      Serial.print(2 * checkalive_interval);
      Serial.println(F("s"));
      
      restart_sim800('e');
    }
}

/*---------------PROCESSING FUNCTIONS--------------*/

// Process SMS
void ProcessSms( String sms, String &from_num ){
  sms.toLowerCase();
  sms.trim();

  
  Serial.print(F("ProcessSms for ["));
  Serial.print(sms);
  Serial.print(F("] lenght = "));
  Serial.println(sms.length());
  
  
  String command = sms.substring(0,sms.indexOf(' '));
  command.trim();
  String arguments = sms.substring(sms.indexOf(' ') + 1);
  arguments.trim();
  //-----------------------------------
  if ( command.equals(F("status")) ){
    send_sms_status(from_num);
    return;
  //-----------------------------------
  } else if ( command.equals(F("aide")) ){
    send_sms_usage(from_num);
    return;
  //-----------------------------------
  } else if ( command.equals(F("chauffage")) ){

    // test only first arg
    if ( arguments.substring(0,arguments.indexOf(' ')) == F("on") ){
      digitalWrite( PIN_HEATER, LOW );
      //Serial.println(F("Start heater"));
      send_sms_to_admins( String("" )
            + F("Chauffage demarre par ")
            + get_contact_name(from_num)
            + ':'
            + from_num
            );

      send_sms(F("Chauffage demarre"),from_num);
      send_sms_status(from_num);
      
      
    } else if ( arguments.substring(0,arguments.indexOf(' ')) == F("off") ) {
      digitalWrite( PIN_HEATER, HIGH );
      //Serial.println(F("Stop heater"));
      send_sms_to_admins( String("" )
            + F("Chauffage arrete par ")
            + get_contact_name(from_num)
            + ':'
            + from_num
            );
      send_sms(F("Chauffage arrete"),from_num);
      send_sms_status(from_num);
      
    } else {
      /*
      Serial.print(F("Unknown arg ["));
      Serial.print(arguments.substring(0,arguments.indexOf(' ')));
      Serial.print(F("] for command ["));
      Serial.println(command +"]" );
      */

      String msg = F("Erreur : Argument inconnu : [");
      msg += arguments.substring(0,arguments.indexOf(' '));
      msg += F("] pour commande : [");
      msg += command;
      msg += F("]");
      msg += F("\naide pour plus d'informations");
      
      send_sms(msg, from_num);
      return;
    }
  //-----------------------------------
  } else if ( command.equals(F("prise")) ){

    // test only first arg
    if ( arguments.substring(0,arguments.indexOf(' ')) == F("on") ){
      digitalWrite( PIN_PLUG, LOW );
      //Serial.println(F("Start plug"));
      send_sms_to_admins( String("" )
            + F("Prise allumee par ")
            + get_contact_name(from_num)
            + ':'
            + from_num
            );

      send_sms(F("Prise allumee"),from_num);
      
    } else if ( arguments.substring(0,arguments.indexOf(' ')) == F("off") ) {
      digitalWrite( PIN_PLUG, HIGH );
      //Serial.println(F("Stop plug"));
      send_sms_to_admins( String("" )
            + F("Prise arretee par ")
            + get_contact_name(from_num)
            + ':'
            + from_num
            );
      send_sms(F("Prise arretee"),from_num);
      
    } else {
      /*
      Serial.print(F("Unknown arg ["));
      Serial.print(arguments.substring(0,arguments.indexOf(' ')));
      Serial.print(F("] for command ["));
      Serial.println(command +"]" );
      */

      String msg = F("Erreur: Argument inconnu : [");
      msg += arguments.substring(0,arguments.indexOf(' '));
      msg += F("] pour commande : [");
      msg += command;
      msg += F("]");
      msg += F("\naide pour plus d'informations");
      
      send_sms(msg, from_num);
      return;
    }
  //-----------------------------------
  } else if ( command.equals(F("ajouter")) ){
    
    String sub_command = arguments.substring(0,arguments.indexOf(' '));
    sub_command.trim();
    arguments = arguments.substring(arguments.indexOf(' ')+1);
    arguments.trim();

    String name, num;
    bool admin;
    name = arguments.substring(0,arguments.lastIndexOf(' '));
    num = arguments.substring(arguments.lastIndexOf(' ')+1);
    name.trim();
    num.trim();
   
    if (sub_command.equals(F("contact"))) {
      // Add regular contact
      admin = false;
    }
    else if (sub_command.equals(F("admin"))) {
      // Add admin contact
      admin = true;
    } else {
      String msg = F("Erreur: Argument inconnu: [");
      msg += sub_command;
      msg += F("] pour commande: [");
      msg += command;
      msg += F("]");
      msg += F("\naide pour plus d'informations");
      
      send_sms(msg, from_num);
      return;
    }
    // ensure srings have the right size
    if ( name.length() > 10 || num.length() != 10 ){
      String msg;
      msg = F("Erreur: Les noms ne doivent pas depasser 10 caracteres.");
      msg += F("\nLes numeros de telephone doivent contenir 10 chiffres");
      msg += F("\naide pour plus d'informations");
      
      send_sms(msg,from_num);
      return;
    }

    add_contact(name,num,admin,from_num);

    /*
    Add entry to SIM phonebook:
    AT+CPBW=,"0606060606",129,"Seb"
    
    List entries : AT+CPBR=1,250
    
    List one entry : AT+CPBR=10
    
    Delete entry 6 : AT+CPBW=6
    */
    return;
  //-----------------------------------
  } else if ( command.equals(F("liste"))){
    String msg;
    for (int i = 0; i < PHONEBOOK_SIZE; i++){
      // check if entry has not been removed
      if ( strlen(phonebook[i].number) > 0 ) {
        msg += String(phonebook[i].name);
        msg += F(":");
        msg += String(phonebook[i].number);
        // only admins can print admin status
        if ( phonebook[i].admin && is_admin(from_num)){
          msg += F("*");
        }
        msg += F("\n");
      }
    }
    send_sms(msg,from_num);
    return;  
  //-----------------------------------
  } else if ( command.equals(F("supprimer")) ){
    String num = arguments.substring(0,arguments.indexOf(' '));
    num.trim();
    del_contact(num,from_num);
    return;  
  //-----------------------------------
  } else if ( command.equals(F("changer")) ){
    String what = arguments.substring(0,arguments.indexOf(' '));
    what.trim();
    arguments.remove(0,arguments.indexOf(' '));
    arguments.trim();

    String where = arguments.substring(0,arguments.indexOf(' '));
    where.trim();
    arguments.remove(0,arguments.indexOf(' '));
    arguments.trim();

    String minmax = arguments.substring(0,arguments.indexOf(' '));
    minmax.trim();
    arguments.remove(0,arguments.indexOf(' '));
    arguments.trim();

    String value = arguments.substring(0,arguments.indexOf(' '));
    value.trim();

    
    if ( what.equals(F("temp"))) {
      if (where.equals(F("int"))){
        if (minmax.equals(F("min"))){
          
          temp_int_low = (short)value.toInt();
          temp_int_min_reached = false;
          EEPROM.put(temp_int_low_address, temp_int_low);
          
        }else if (minmax.equals(F("max"))){
          
          temp_int_high = (short)value.toInt();
          temp_int_max_reached = false;
          EEPROM.put(temp_int_high_address, temp_int_high);
                    
        }else{
          send_sms( String("") 
                    + F("Erreur: valeur [")
                    + minmax
                    + F("] inconnue pour commande [")
                    + command + ' ' + what + +' ' + where
                    + F("]")
                   , from_num );
          return;
          
        }
        
      } else if (where.equals(F("ext"))) {
        if (minmax.equals(F("min"))){
          
          temp_ext_low = (short)value.toInt();
          temp_ext_min_reached = false;
          EEPROM.put(temp_ext_low_address, temp_ext_low);
          
        }else if (minmax.equals(F("max"))){
            
          temp_ext_high = (short)value.toInt();
          temp_ext_max_reached = false;
          EEPROM.put(temp_ext_high_address, temp_ext_high);
                    
        }else{
          send_sms( String("") 
                    + F("Erreur: valeur [")
                    + minmax
                    + F("] inconnue pour commande [")
                    + command + ' ' + what + +' ' + where
                    + F("]")
                   , from_num );
          return;
          
        }

        
      } else {
        send_sms( String("") 
                  + F("Erreur: valeur [")
                  + where
                  + F("] inconnue pour commande [")
                  + command + ' ' + what
                  + F("]")
                 , from_num );
        return;
        
      }
      
    }else {
      send_sms( String("") 
                + F("valeur [")
                + what
                + F("] inconnue pour commande [")
                + command
                + F("]")
               , from_num );
       return;
    }

    send_sms(F("Valeur mise a jour"),from_num);
    return;  
  //-----------------------------------
  } else if ( command.equals(F("reset")) ){
    String val = arguments.substring(0,arguments.indexOf(' '));
    val.trim();
    if ( val.equals(F("minmax"))){
      temp_int_min = temp_int;
      temp_int_max = temp_int;
      temp_ext_min = temp_ext;
      temp_ext_max = temp_ext;
      
      send_sms(F("Valeurs min et max reinitialises pour toutes les temperatures."),from_num);
      
    } else {
      send_sms( String("")
                + F("Erreur: Valeur inconnue [")
                + val
                + F("] pour la commande [")
                + command
                + ']'
                , from_num);
                
    }
    return;  
  //-----------------------------------
  } else if ( command.equals(F("ping")) ){
    send_sms(F("pong"),from_num);
    return;  
  //-----------------------------------
  } else if ( command.startsWith(F("bonjour")) || command.startsWith(F("coucou")) || command.startsWith(F("salut")) || command.startsWith(F("hello"))  ){
    send_sms(F("Bien le bonjour a vous aussi !"),from_num);
    return;
  //-----------------------------------
  } else {

    
    String msg = F("Erreur : Commande inconnue: [");
    msg += command;
    msg += F("]");
    msg += F("\naide pour plus d'informations");
    send_sms(msg, from_num);
    
    return;
  }

  return;
}


// interpret the GPRS shield message and act appropiately
void ProcessGprsMsg() {
  // Remove trailer char
  msg.remove(0,1);

    
  Serial.print(F("\nGPRS Message: ["));
  Serial.print(msg);
  Serial.println(F("]"));

  // Reset checkalive on OK message
  if (msg.equals(F("OK"))){
    Serial.println(F("OK catched !"));
    last_ok_received = (unsigned long)millis();
  }

  // Process Call ready to start SMS text mode
  else if( msg.equals(F("SMS Ready"))){
    Serial.println(F("GPRS Shield registered on Mobile Network"));
    sim800.println(F("AT+CMGF=1"));
  }
  // Process normal power down to restart the shied
  else if (msg.equals(F("NORMAL POWER DOWN"))){ 
    Serial.println(F("Normal powerdown received; restart sim800"));
    restart_sim800('r');
  }

  // SMS message
  else if( msg.startsWith(F("+CMTI"))){
    Serial.println(F("*** SMS Received ***"));
    // Look for the coma in the full message (+CMTI: "SM",6)
    // In the sample, the SMS is stored at position 6

    // Ask to read the SMS store
    sms_position = msg.substring(msg.indexOf(',') + 1);
    sim800.print(F("AT+CMGR="));
    sim800.println( sms_position );
    
  }

  // SMS store readed through UART (result of GMGR request)  
  else if( msg.indexOf(F("+CMGR:")) >= 0 ){
    // Example of message : +CMGR: "REC UNREAD","+33606060606","Name","20/11/29,14:59:33+04"
    
    // get from telephone number
    int pos = msg.indexOf(',')+2;
    msg.remove(0,pos);
    from = msg.substring(0,msg.indexOf('"'));
    // remove third first char and add a 0 to change from international (+33 or 0033)
    // to national number to compare it with phonebook
    if (from.startsWith(F("+"))) {
      from = '0' + from.substring(3);
    } else if (from.startsWith(F("00"))) {
      from = '0' + from.substring(4);
    }
    
    // Next message will contains the BODY of SMS
    SmsContentFlag = 1;
    // Following lines are essentiel to not clear the flag!
    msg="";
    return;
  }
/* Read SIM phonebook entries on a CPBF search command
 *  
 
  else if (msg.startsWith(F("+CPBF:")) >= 0){
    
    // example of string received : +CPBF: 10,"0606060606",129,"Name"
    int next;
    String id, num, name;
    next = msg.indexOf(',');
    id = msg.substring(8,next);
    
    // look fun num, +2 to consume the "
    msg.remove(0,next+2);
    next = msg.indexOf('"');
    num = msg.substring(0,next);
    
    // Next val is phone type (national = 129 international = 145
    // Always coded on 3 digits
    // jump of 7 to start the name
    msg.remove(0, next + 7);
    next = msg.indexOf('"');
    name = msg.substring(0,next);
    
    Serial.println("*** CPBF *** id=[" + String(id) +"] num=[" + String(num) + "] name=[" + String(name) + "]");
   
  }
*/

  // +CMGR message just before indicate that the following GRPS Shield message 
  // (this message) will contains the SMS body
  if( SmsContentFlag == 1 ){
    Serial.println(F("*** SMS MESSAGE CONTENT ***"));
    Serial.println( msg );
    Serial.println(F("*** END OF SMS MESSAGE ***"));
    // Process SMS only coming from registered contacts
    if (is_registered(from)) {
      ProcessSms( msg, from );
    } else {
      Serial.print(F("Contact not registered :"));
      Serial.println(from);
    }
    //Serial.print(F("Remove SMS a position "));
    //Serial.println(sms_pos);

    sim800.print(F("AT+CMGD="));
    sim800.println(sms_position);
    
    
  }

  // Always clear Gprs message
  msg="";
  // Always clear the flag
  SmsContentFlag = 0; 
}


/*---------------PHONEBOOK FUNCTIONS--------------*/

bool is_registered (String &num) {
  return get_contact_id(num) >= 0;
}

bool is_admin(String &num) {
  int i = get_contact_id(num);
  return phonebook[i].admin;
}

String get_contact_name(String &num){
  // check if num is already registered
  for (int i=0; i < PHONEBOOK_SIZE; i++ ){
    if ( String(phonebook[i].number) == num ){
      return  String(phonebook[i].name);
    }
  }
  return F("inconnu");
}

int get_contact_id(String &num){
  // check if num is already registered
  for (int i=0; i < PHONEBOOK_SIZE; i++ ){
    if ( String(phonebook[i].number) == num ){
      return i;
    }
  }
  return -1;
}

int nb_admins() {
  int nb =0;
  for (int i=0; i < PHONEBOOK_SIZE; i++ ){
    if ( phonebook[i].admin ){
      nb++;
    }
  }

  return nb;
}

void del_contact(String &num, String &from_num) {
  if (is_registered(num)){
    int i = get_contact_id(num);

    // only admins can remove admins contacts
    if (is_admin(from_num)){
      if ( nb_admins() > 1 || !is_admin(num)){
        send_sms_to_admins( String("" )
                    + F("Contact ")
                    + phonebook[i].name
                    + ':'
                    + phonebook[i].number
                    + F(" ")
                    + F(" supprime par ")
                    + get_contact_name(from_num)
                    + ':'
                    + from_num
                    );
        phonebook[i] = EmptyContact;
        EEPROM.put(phonebook_address, phonebook);

        send_sms(F("Contact supprime"), from_num);
        return;
      } else {
        send_sms(F("Erreur: Impossible de supprimer le seul admin."), from_num);
        return;
      }
    }
    else {
      if (!is_admin(num)) {
        send_sms_to_admins( String("")
            + F("Contact ")
            + phonebook[i].name
            + ':'
            + phonebook[i].number
            + F(" ")
            + F(" supprime par ")
            + get_contact_name(from_num)
            + ':'
            + from_num
            );
        phonebook[i] = EmptyContact;
        EEPROM.put(phonebook_address, phonebook);

        send_sms(F("Contact supprime"), from_num);
        return;
      } else {
        send_sms(F("Erreur: Erreur inconnue"), from_num);
        return;
      }
    }
  }  
  String msg;
  msg += F("Erreur: Numero non enregistre : ");
  msg += num;
  send_sms(msg, from_num);
  return;
}

void add_contact(String name, String num,  bool admin, String &from_num) {
 
  int i=0;
  bool already_exists = false;
  // check if num is already registered
  i = get_contact_id(num);
  // if the contact do not already exists; get the first empty one.
  if ( i < 0 ){
    // find first empty
    i=0;
    while (strlen(phonebook[i].number) > 0 && i < PHONEBOOK_SIZE){i++;}
  } else {
    already_exists = true;
  }

  // Check if the phonebook is full
  if ( i == PHONEBOOK_SIZE ) {
    String nb_contacts;

    if (is_admin(from_num)) {
      nb_contacts = String("") + F("\nNombre de contacts maximum : ") + PHONEBOOK_SIZE;
    }
    send_sms( String("") 
              + F("Erreur :  Plus de place disponible.")
              + nb_contacts
              , from_num);
    return;
  }


  if ( is_admin(from_num) || ! admin) {
    name.toCharArray(phonebook[i].name, 11);
    num.toCharArray(phonebook[i].number, 11);
    phonebook[i].admin = admin;
    EEPROM.put(phonebook_address, phonebook);

    String action;
    if ( already_exists ){
      action = F("mis a jour");
    } else {
      action = F("ajoute");
    }
    send_sms_to_admins( String("" )
                        + F("Contact ")
                        + phonebook[i].name
                        + ':'
                        + phonebook[i].number
                        + F(" ")
                        + action
                        + F(" par ")
                        + get_contact_name(from_num)
                        + ':'
                        + from_num
                        );
                       
    send_sms( String("")
              + F("Contact ")
              + action
              + F(" !")
              , from_num);

  } else {
    send_sms(F("Erreur: Erreur inconnue"), from_num);
    return;
  }

}

/*---------------TOOLS FUNCTIONS--------------*/


void send_sms (String content, String phone_number) {

  content.trim();
  Serial.print(F("SMS length = "));
  Serial.println(content.length());
  
  flush_sim800();
  sim800.print(F("AT+CMGS=\""));
  sim800.print(phone_number);
  sim800.println(F("\""));
  wait_answer();
  sim800.println(content + char(26) + "");
  wait_answer();
  
}

void send_sms_to_admins(String &content){
  for (int i=0; i < PHONEBOOK_SIZE; i++ ){
    if ( phonebook[i].admin ){
      send_sms(content, String(phonebook[i].number));
    }
  }
 
}

void send_sms_status(String &phone_number) {

  String heater, plug;

  if ( !digitalRead(PIN_HEATER))
    heater = F("ON");
  else
    heater = F("OFF");

  if ( !digitalRead(PIN_PLUG))
    plug = F("ON");
  else
    plug = F("OFF");

  
  String msg;
  msg += F("Chauffage: ");
  msg += heater;
  msg += F("\nPrise: ");
  msg += plug;
  msg += F("\n");
  
  msg += F("T int : ");
  msg += String(temp_int);
  msg += F(" C\n");

  msg += F("T int alertes : ");
  msg += String(temp_int_low);
  msg += F(" | ");
  msg += String(temp_int_high);
  msg += F("\n");
  
  msg += F("T int min/max : ");
  msg += String(temp_int_min);
  msg += F("/");
  msg += String(temp_int_max);
  msg += F("\n");
  
  msg += F("H int : ");
  msg += String(hum_int);
  msg += F(" %\n");
  
  msg += F("T ext : ");
  msg += String(temp_ext);
  msg += F(" C\n");

  msg += F("T ext alertes : ");
  msg += String(temp_ext_low);
  msg += F(" | ");
  msg += String(temp_ext_high);
  msg += F("\n");

  msg += F("T ext min/max : ");
  msg += String(temp_ext_min);
  msg += F("/");
  msg += String(temp_ext_max);
  msg += F("\n");
  
  msg += F("H ext : ");
  msg += String(hum_ext);
  msg += F(" %\n");

  msg += F("ON depuis ");
  msg += uptime();

  send_sms(msg, phone_number);
}

void send_sms_usage(String &phone_number) {
  String msg;

  msg += F("Aide : \n\n");
  msg += F("status\n  status des relais/capteurs\n");
  msg += F("chauffage <on|off>\n");
  msg += F("prise <on|off>\n");
  msg += F("changer temp <int|ext> <min|max> <valeur>\n");
  msg += F("reset minmax\n");
  msg += F("ajouter contact <nom> <numero>\n");

  if ( is_admin(phone_number)) {
    msg += F("ajouter admin <nom> <numero>\n");
  }

  msg += F("supprimer <numero>\n");
  msg += F("liste\n  liste les contacts\n");


  send_sms(msg, phone_number);
}

/*---------------SIM800 FUNCTIONS--------------*/


void flush_sim800() {
  while (sim800.available()) {
     Serial.write(sim800.read());
  }
}

void wait_answer() {
  unsigned long start_time = millis();
  int timeout = 5000;
  String  ret;
   
  // wait available
  while ( ret.indexOf(F("OK")) <= 0 && ret.indexOf(F("ERROR")) && (millis() - start_time) < timeout) {
      ret = sim800.readString();
      Serial.print(ret);
  }
}


//  Force a restart using RST PIN of SIM800
void restart_sim800(char reason) {
  
  // Start Sim800
  Serial.println(F("Reset sim800"));
  digitalWrite(PIN_SIM800_RST, LOW);
  delay(5000);
  digitalWrite(PIN_SIM800_RST, HIGH);
  delay(1000);
  digitalWrite(PIN_SIM800_RST, LOW);
  delay(1000);
  digitalWrite(PIN_SIM800_RST, HIGH);
  delay(5000);


  flush_sim800();
  sim800.println(F("AT"));
  wait_answer();

  // Configure phonebook to use SIM phonebook
  //sim800.println(F("AT+CPBS=\"SM\""));

  // enable time GPRS sync
  //sim800.println(F("AT+CLTS=1;&W"));
  //delay(500);
  //sim800.println(F("AT+CFUN=1,1"));

  Serial.println(F("sim800 restarted !"));

  // reset checkalive
  last_ok_received = (unsigned long)millis();

  sim800.println(F("AT+CMGF=1"));
  wait_answer();

  // Clear all old messages received
  flush_sim800();
  sim800.println(F("AT+CMGDA=\"DEL ALL\""));
  wait_answer();
  
  
  delay(1000);
  flush_sim800();
  
  
  String sms_msg = F("Systeme demarre.");
  switch (reason) {
  case 'e':
    sms_msg += F("\nRedemarrage suite erreur puce GSM.");
    break;
  case 'r':
    sms_msg += F("\nDemarrage suite reset puce GSM.");
    break;
  default:
    sms_msg += F("\nDemarrage initial.");
    break;
  }
  send_sms_to_admins ( sms_msg);
  
}

/*---------------TOOLS FUNCTIONS--------------*/


String uptime() {
  unsigned long m = millis() / 1000;
  String ret;

  String hours = String(numberOfHours(m), DEC);
  if ( numberOfHours(m) < 10)
    hours = "0" + hours;

  String minutes = String(numberOfMinutes(m), DEC);
  if ( numberOfMinutes(m) < 10)
    minutes = "0" + minutes;

  String seconds = String(numberOfSeconds(m), DEC);
  if ( numberOfSeconds(m) < 10)
    seconds = "0" + seconds;

  ret = String(elapsedDays(m),DEC);
  ret += F(" jours ");
  ret += hours + ':' + minutes + ':' + seconds;


  return(ret);
   
}
