#include "ESPNetworkManager.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <PubSubClient.h> //Βιβλιοθήκη για δημιουργία πελάτη Mqtt (Nick O'Leary)

/** Αρχικοποίηση μεταβλητών */
//Δημιουργια αναγνωρηστικου Μικροελεγκτή ModuleID
String ModuleID = String(ESP.getChipId());
String mcu_type = "output"; // O Μικροελεγκτής λειτουργει ως έξοδος δεδομένων
//Θέματα στα οποία δέχεται και στέλνει Mqtt Μηνύματα ο Μικροελεγκτής
String myTopic = ("/" + mcu_type + "/" + ModuleID + "/" + "data");
String settingsTopic = ("/" + mcu_type + "/" + ModuleID + "/" + "settings");
String willTopic = ("/" + mcu_type + "/" + ModuleID + "/" + "lastwill");
String controlType =
    "analog"; //Τρόπος ελέγχου των συσκευών(ControlType). Χρησιμοποιείτε
              //για την σωστή δημιουργία εργαλείων ελέγχου στις συσκευές Android

//Αρχικοποίηση μεταβλητών πελάτη Mqtt (PubSubClient)
const char *micro_mqtt_broker = "educ.chem.auth.gr";
const char *mqtt_username;
const char *mqtt_password;
int retries = 0;
WiFiClient sclient;
PubSubClient mqtt_client(sclient);

//Αρχικοποίηση βιβλιοθήκης που αναλαμβάνει την "κατάσταση
//ρύθμισης" του Esp8266
EspNetworkManager netmanager(ModuleID);

//Αρχικοποίηση μεταβλητών σύνδεσης Wi-Fi
String WiFiSSID;
String WiFiPassword;

//Αρχικοποίηση αισθητήρα Καπνού MQ-2
const int AOUTGaspin = A0; //Ακροδέκτης αναγολικής τιμής αισθητήρα
int Gaslimit;     //Επικινδυνο όριο αισθητήρα
int Gasvalue;     //τιμή αισθητήρα
long lastMsg = 0; //στιγμη τελευταιας μετρησης
char msg[50];
int value = 0;
int datasets = 0; //αριθμός μετρήσεων

//Δηλώσεις Μεθόδων
void ExplicitRunNetworkManager(
    void); //Μέθοδος που καλεί την βιβλιοθήκη EspNetworkManager
void onMessageReceived(
    char *, byte *,
    unsigned int); // Callback που καλείτε όταν ληφθεί μήνυμα MQTT
boolean ConnectToNetwork(void); //Μέθοδος σύνδεσης σε τοπικό δίκτυο Wi-Fi
void MqttReconnect(void); //Μέθοδος σύνδεσης - επανασύνδεσης σε μεσίτη MQTT

void setup() {
  EEPROM.begin(512);
  Serial.begin(115200); //Σειριακή επικοινωνία για debugging με baudrate 115200

  /**Αξιολόγηση αν χρειάζεται η "Λειτουργία ρυθμίσεων". Αν μπορει να συνδεθεί σε
   * τοπικό
   * δίκτυο ,τότε  δεν χρειάζεται.*/
  if (!ConnectToNetwork()) {
    //Αν δεν μπορέσεις να συνδεθείς στο δίκτυο ξεκίνα την "Λειτουργία Ρυθμίσεων"
    //μέσω την βιβλιοθήκης EspNetworkManager
    ExplicitRunNetworkManager();
  }
  mqtt_client.setServer(netmanager.ReadBrokerAddress().c_str(), 1883);
  mqtt_client.setCallback(onMessageReceived);
}

/*Κεντρική λούπα προγράμματος*/
void loop() {
  //Πάντα προσπαθεί  να συνδεθεί στο μεσίτη, επαναλαμβάνει αν δεν τα καταφέρει.
  if (!mqtt_client.connected()) {
    MqttReconnect();
  }
  mqtt_client.loop();              //Μέθοδος που καλείτε για να λειτουργήσει το callback των
                                   //μηνυμάτων MQTT
                                   /*Συνεχέις μέτρηση απόστασης με τον αισθητήρα VL53L0X*/
  long now = millis();             //Μέτρηση χρόνου
  String message = "Out of Range"; //Αρχικοποίηση ειδοποίησης
  if (now - lastMsg >
      6000) { //Αν πέρασαν 6 δευτερολεπτα απο την προηγούμενη μέτρηση
    Gasvalue = analogRead(
        AOUTGaspin); //Διαβασε την την binary τιμη του αισθητήρα απο τον ADC
    lastMsg = now;
    String message = (String)Gasvalue;
    mqtt_client.publish(myTopic.c_str(), message.c_str()); //Αποστολή μέτρησης
  }
}

void MqttReconnect() {
  // Προσπάθησε να συνδεθείς μέχρι να τα καταφερεις
  Serial.print("Connecting to Push Service...");
  // Συνδέσου στον server που ορίσαμε με username: androiduse, password:and3125
  //Οι μεταβλητές αυτές διαβάζονται από την βιβλιοθήκη EspNetworkManager η οποία
  //τις ανακαλέι απο την ROM
  if (!mqtt_client.connect(ModuleID.c_str(),
                           netmanager.ReadBrokerUsername().c_str(),
                           netmanager.ReadBrokerPassword().c_str())) {
    delay(1000);
    retries++;
    if (retries <= 4) {
      Serial.print("Failed to connect, mqtt_rc=");
      Serial.print(mqtt_client.state()); //Αποσφαλμάτωση αποτυχίας σύνδεσης
      Serial.println(" Retrying in 2 seconds");
      retries++;
    } else {
      retries = 0;
      ExplicitRunNetworkManager(); //Εκκίνηση της "Λειτουργίας ρυθμίσεων"
    }
    return;
  } else {
    delay(500);
    Serial.println("Connected Sucessfully");
    //Εγγραφή στα απαραίτητα θέματα
    mqtt_client.subscribe("/android/synchronize"); //Θέμα συγχρονισμού Android
    mqtt_client.subscribe(myTopic.c_str()); //Θέμα αποστολής δεδομένων αισθητήρα
    mqtt_client.subscribe(settingsTopic.c_str()); //θέμα ρυθμίσεων μΕ
  }
}

void ExplicitRunNetworkManager() {
  netmanager.begin(); //Αρχικοποίηση EspNetworkManager
  if (!netmanager.runManager()) {
    Serial.println("Manager Failed");
  } else //Η διεργασίες της βιβλιοθήκης EspNetworkManager εκτελέστηκα επιτυχώς ο
  //χρήστης καταχώρησε κωδικούς και ονόματα
  {
    Serial.println("Credentials Received");
    Serial.println(
        netmanager.ReadSSID()); //Δειξε το ονομα δικτυου που διάλεξε ο χρήστης
    Serial.println(netmanager.ReadPASSWORD()); //Δείξει τον κωδικο δικτυου
  }
}

void onMessageReceived(char *topic, byte *payload,
                       unsigned int length) /*Λήφθηκε Μήνυμα Mqtt*/
{
  //Δείξε το Mqtt θέμα μέσω σειριακής
  Serial.print("Inoming Message [Topic: ");
  Serial.print(topic);
  Serial.print("] Message: ");
  String androidID;
  for (int i = 0; i < length;
       i++) //Μετατροπή μηνύματος σε String και προβολή του
  {
    Serial.print((char)payload[i]);
    androidID += (char)payload[i];
  }
  Serial.println();
  if (strcmp(topic, "/android/synchronize") ==
      0) /*Μια συσκευή Android ζήτησε συγχρονισμό */
  {
    Serial.println("Android asking for mC ID");
    //Το μήνυμα περιέχει το ID της συσκευής android. Ετσι μπορούμε να βρούμε το
    //θέμα(newAndroidTopic)
    //οπού πρέπει να στείλουμε τις πληροφορίες αυτής της συσκευής.
    String newAndroidTopic = ("/android/" + androidID + "/newmodules");
    //Δημιούργησε τα string που περιέχουν όλες τις πληροφορίες αυτής της
    //συσκευής.
    //Κάθε μεταβλητή χωρίζεται με τον χαρακτήρα "&" ώστε να μπορει να διαβαστεί
    //από την συσκευή Android
    String myInfo = (ModuleID + "&" + mcu_type + "&" + controlType + "&" +
                     "Gas Sensor"); //Πληροφορίες αισθητήρα για δημιουργια
                                    //αντικειμένου απο την συσκευή Android
    mqtt_client.publish(
        (newAndroidTopic.c_str()),
        (myInfo.c_str())); //Αποστολή πληροφοριών στης συσκευή που τις ζήτησε
    delay(2);
  }
}

boolean ConnectToNetwork() {
  WiFi.mode(WIFI_STA); //Ο μΕ γίνεται Wi-Fi Station
  delay(10);
  Serial.println("WIFI>>:Attempting Connection to ROM Saved Network");
  String netssid = netmanager.ReadSSID(); //Ανάγνωση SSID δικτύου από την ROM
  Serial.println("WIFI>>Network: " + netssid);
  String netpass =
      netmanager.ReadPASSWORD(); //Ανάγνωση Κωδικού δικτύου από την ROM
  WiFi.begin(netssid.c_str(), netpass.c_str()); // Σύνδεση
  int attemps = 0;
  while (WiFi.status() != WL_CONNECTED) //Έλεγχος αν είναι συνδεδεμένο
  {
    delay(500);
    Serial.print(".");
    if (WiFi.status() == WL_CONNECT_FAILED ||
        attemps > 20) //Η σύνδεση θεωρείται αποτυχημένη μετα από 20 προσπάθειες
    {
      Serial.println("");
      Serial.println("WIFI>>:Connection Failed. Starting Network Manager.");
      delay(10);
      return false;
    }
    attemps++;
  } //Επιτύχής σύνδεση
  Serial.println("");
  Serial.println("WIFI>>:Connected");
  Serial.print("WIFI>>:IPAddress: ");
  Serial.println(WiFi.localIP());
  return true;
}
