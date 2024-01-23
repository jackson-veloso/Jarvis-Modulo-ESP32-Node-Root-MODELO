#include <Arduino.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "ArduinoJson.h"
#include "IPAddress.h"
#include <HTTPClient.h>
#include "painlessMesh.h"

#define MESH_PREFIX "whateverYouLike"
#define MESH_PASSWORD "somethingSneaky"
#define MESH_PORT 5555
#define ID 1

// Configurar a rede WIFI
#define STATION_SSID "ssid"
#define STATION_PASSWORD "senha"
#define HOSTNAME "HTTP_BRIDGE"

// WATCHDOG
hw_timer_t *timer = NULL; // faz o controle do temporizador (interrupção por tempo)

// função que o temporizador irá chamar, para reiniciar o ESP32
void IRAM_ATTR resetModule()
{
  ets_printf("(watchdog) reiniciar\n"); // imprime no log
  esp_restart();                        // reinicia o chip
  // esp_restart_noos(); //não funcionou foi substituída pela linha de cima.
}

byte contCheckConnection = 0;
byte contCheckConnectionRoot = 0;
JsonDocument modulosOnline;

Scheduler userScheduler;
painlessMesh mesh;

AsyncWebServer server(80);
IPAddress myIP(0, 0, 0, 0);
IPAddress myAPIP(0, 0, 0, 0);
IPAddress jarvisHub(192, 168, 1, 10); // Configurar conforme projeto

// Prototype
void webServerEnable();
void receivedCallback(uint32_t from, String &msg);
void getMessageHello(JsonDocument myObject);
void sendMessageHello();
void checkConnection();
void checkConnectionRoot();
void printRedeMesh();
bool sendInfoIPConnectionRoot();
IPAddress getlocalIP();

Task taskSendMessageHello(TASK_SECOND * 11, TASK_FOREVER, &sendMessageHello);
Task taskCheckConnection(TASK_SECOND * 20, TASK_FOREVER, &checkConnection);
Task taskCheckConnectionRoot(TASK_SECOND * 20, TASK_FOREVER, &checkConnectionRoot);
Task taskPrintRedeMesh(TASK_SECOND * 5, TASK_FOREVER, &printRedeMesh);

void printRedeMesh()
{
  Serial.println();
  Serial.println(" ========== ====== ====== ");
  Serial.println(mesh.subConnectionJson());
  Serial.println(" ========== ====== ====== ");
}

void sendMessageHello()
{
  JsonDocument hello;
  hello["code"] = 100;
  hello["id"] = ID;

  String json;
  serializeJson(hello, json);
  mesh.sendBroadcast(json);
  Serial.print("Send Message Hello = ");
  Serial.println(json);
}

void getMessageHello(JsonDocument doc)
{
  byte id = doc["id"];

  Serial.print("ID recebido = ");
  Serial.println(id);

  modulosOnline[id] = "true";
}

void checkConnection()
{
  // Se a Lista de Node igual a 01 reinicia a rede mesh para tentar se reconectar
  contCheckConnection++;
  Serial.print("NodeList = ");
  Serial.println(mesh.getNodeList(true).size());
  Serial.print("total de verificações de conexão ativa: ");
  Serial.println(contCheckConnection);

  if (mesh.getNodeList(true).size() == 1)
  {
    if (contCheckConnection > 6)
    {
      Serial.println(" ###### ==== ##### REINICIAR ESP32 ******&&&&&******&&&&");
      esp_restart(); // reinicia o chip
    }
  }
  else
  {
    contCheckConnection = 0;
  }
}

void checkConnectionRoot()
{
  Serial.print("Verifica conexao do Root / Device Is Root? = ");

  if (!sendInfoIPConnectionRoot())
  {
    contCheckConnectionRoot++;

    if (contCheckConnectionRoot > 6)
    {
      Serial.println(" ###### ==== ##### = Root Sem Conexão - REINICIAR ESP32 ******&&&&&******&&&&");
      esp_restart(); // reinicia o chip
    }
  }
  else
  {
    contCheckConnectionRoot = 0;
  }
}

bool sendInfoIPConnectionRoot()
{
  //Teste com o Google
  HTTPClient http;
  http.begin("http://www.google.com.br");
  int httpCode = http.GET(); // inicia uma conexão e envia um cabeçalho HTTP para o URL do servidor configurado
  Serial.print("[HTTP] GET... código: ");
  Serial.println(httpCode);

  
  // HTTPClient http;
  String urlBase = "http://" + jarvisHub.toString() + "/noderoot";
  // http.begin(urlBase);
  // http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["id"] = ID;
  doc["description"] = "node root";
  doc["ip"] = myIP.toString();
  
  String json;
  serializeJson(doc,json);

  // int httpCode = http.POST(json);
  
  Serial.print("URL BASE = ");
  Serial.println(urlBase);
  Serial.println(json);

  if (httpCode == 200 || httpCode == 201)
  {
    return true;
  }
  return false;
}

IPAddress getlocalIP()
{
  return IPAddress(mesh.getStationIP());
}

void receivedCallback(uint32_t from, String &msg)
{
  Serial.printf("startHere: Received from %u msg= %s\n", from, msg.c_str());
  Serial.println();

  JsonDocument doc;
  deserializeJson(doc, msg.c_str());

  byte code = doc["code"];

  switch (code)
  {
  case 100:
    getMessageHello(doc);
    break;

  case 200:
    break;

  default:
    break;
  }
}

void newConnectionCallback(uint32_t nodeId)
{
  Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
  Serial.println();
}

void changedConnectionCallback()
{
  Serial.printf("Changed connections\n");
  Serial.println();
}

void nodeTimeAdjustedCallback(int32_t offset)
{
  Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(), offset);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);

  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);

  // Configurar o canal do AP, deverá ser o mesmo para toda a rede mesh e roteador wifi
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT, WIFI_AP_STA, 3);

  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskSendMessageHello);
  userScheduler.addTask(taskCheckConnectionRoot);
  userScheduler.addTask(taskCheckConnection);
  userScheduler.addTask(taskPrintRedeMesh);

  taskSendMessageHello.enable();
  taskCheckConnectionRoot.enable();
  taskCheckConnection.enable();
  taskPrintRedeMesh.enable();

  mesh.stationManual(STATION_SSID, STATION_PASSWORD);
  mesh.setHostname(HOSTNAME);

  // Bridge node, should (in most cases) be a root node. See [the wiki](https://gitlab.com/painlessMesh/painlessMesh/wikis/Possible-challenges-in-mesh-formation) for some background
  mesh.setRoot(true);
  // This node and all other nodes should ideally know the mesh contains a root, so call this on all nodes
  mesh.setContainsRoot(true);

  myAPIP = IPAddress(mesh.getAPIP());
  Serial.println("My AP IP is " + myAPIP.toString());

  //inicia o webserver
  webServerEnable();

  // WATCHDOG
  // hw_timer_t * timerBegin(uint8_t num, uint16_t divider, bool countUp)
  /*
     num: é a ordem do temporizador. Podemos ter quatro temporizadores, então a ordem pode ser [0,1,2,3].
    divider: É um prescaler (reduz a frequencia por fator). Para fazer um agendador de um segundo,
    usaremos o divider como 80 (clock principal do ESP32 é 80MHz). Cada instante será T = 1/(80) = 1us
    countUp: True o contador será progressivo
  */
  timer = timerBegin(0, 80, true); // timerID 0, div 80
  // timer, callback, interrupção de borda
  timerAttachInterrupt(timer, &resetModule, true);
  // timer, tempo (us), repetição
  timerAlarmWrite(timer, 10000000, true); // reinicia após 10s
}

void loop()
{
  mesh.update();
  if (myIP != getlocalIP())
  {
    myIP = getlocalIP();
    Serial.println("My IP is " + myIP.toString());
  }

  // WATCHDOG
  timerWrite(timer, 0); // reseta o temporizador (alimenta o watchdog)
}


void webServerEnable(){
   // Async webserver
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    request->send(200, "text/html", "<form>Text to Broadcast<br><input type='text' name='BROADCAST'><br><br><input type='submit' value='Submit'></form>");
    if (request->hasArg("BROADCAST")){
      String msg = request->arg("BROADCAST");
      mesh.sendBroadcast(msg);
    } });

  // Async webserver
  server.on("/modulos", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    request->send(200, "text/html", "<form>Text to Broadcast<br><input type='text' name='BROADCAST'><br><br><input type='submit' value='Submit'></form>");
    
  String json;
  serializeJson(modulosOnline, json);
  mesh.sendBroadcast(json);

  modulosOnline.clear();
  
    if (request->hasArg("BROADCAST")){
      String msg = request->arg("BROADCAST");
      mesh.sendBroadcast(msg);
    } });

  server.begin();
}