#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "nvs_flash.h"
#include <MD5Builder.h>
#include <PubSubClient.h> //https://github.com/knolleary/pubsubclient
#include <ESPAsyncWebServer.h> //https://github.com/me-no-dev/ESPAsyncWebServer
#include <map>

//#define MYSSID "myssid"
//#define PASSWD "mypasswd"
#define KEY_MAX_SIZE 1984 //max size of an nvs key

nvs_handle nvs_mqtt;
AsyncWebServer server(80);
std::map<uint32_t,String> requestMap;

void set_nvs(const char* key, const char* value) {
  esp_err_t err = nvs_set_str(nvs_mqtt, key, value);
  if (err != ESP_OK) {
    Serial.print("NVS Error: ");
    Serial.println(err);
  }
}

String get_nvs (const char* key) {
  size_t required_size = KEY_MAX_SIZE;
  nvs_get_str(nvs_mqtt, key, NULL, &required_size);
  char result[required_size];
  esp_err_t err = nvs_get_str(nvs_mqtt, key, result, &required_size);
  if (err != ESP_OK) {
    Serial.print("NVS Error: ");
    Serial.println(err);
    return "";
  }
  return String(result);
}

String print_nvs (const char* key, const String val) {
  String html = "<br>" + String(key) + ":  ";
  if (val=="") return html;
  if (strstr(key,"cert")) {
    MD5Builder md5;
    md5.begin();
    md5.add(val);
    md5.calculate();
    html += md5.toString() + " (md5)";
  } else {html += String(val);}
  Serial.println(html);
  return html;
}

String runTest() {
  String mqtt_id, mqtt_addr;
  String root_ca_pem, certificate_pem_crt, private_pem_key;
  String result = "<html><head></head><body><h2>Testing MQTT Connection</h2>";
  mqtt_id = get_nvs("mqtt_id");
  result += print_nvs("mqtt_id",mqtt_id);
  mqtt_addr = get_nvs("mqtt_addr");
  result += print_nvs("mqtt_addr",mqtt_addr);
  root_ca_pem = get_nvs("root_cert");
  result += print_nvs("root_cert",root_ca_pem);
  certificate_pem_crt = get_nvs("client_cert");
  result += print_nvs("client_cert",certificate_pem_crt);
  private_pem_key = get_nvs("cert_key");
  result += print_nvs("cert_key",private_pem_key);

  WiFiClientSecure net;
  net.setCACert(root_ca_pem.c_str());
  net.setCertificate(certificate_pem_crt.c_str());
  net.setPrivateKey(private_pem_key.c_str());

  PubSubClient thing(mqtt_addr.c_str(), 8883, net);
  thing.connect(mqtt_id.c_str());
  if (thing.connected()) {
    result += "<h2>Connection successful!</h2>";
  } else {
      result += "<h2>Connection failed.</h2>";
      char err_buf[100];
      if (net.lastError(err_buf,100)<0) {
        result += "<br>" + String(err_buf);
      } else {
        result += "<br>MQTT Error: " + String(thing.state());
      }
  }
  return result;
}

void completeFile(const uint32_t mapkey, const String keyName, const uint32_t filesize) {
  bool valid_file = false;
  if (keyName == "root_cert" || keyName == "client_cert" || keyName == "cert_key") 
     valid_file = true;
  if (filesize > KEY_MAX_SIZE) valid_file = false;
  if (valid_file) {
    esp_err_t err = nvs_set_str(nvs_mqtt, keyName.c_str(), requestMap[mapkey].c_str());      
  } else {
    Serial.println("Didn't find cert type");
  }
  requestMap.erase(mapkey);
}

void handleUpload(AsyncWebServerRequest *request,const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  AsyncWebParameter* p = request->getParam(request->params()-1);
  Serial.print(request->params());
  Serial.print(" Upload ");
  Serial.println(p->name());
//  if (index == 0 && requestMap[(uint32_t)request]) {
//    completeFile((uint32_t)[request],"??",requestMap[(uint32_t)request].length())
//  }
  if (final) Serial.println(filename);
  String holder;
  holder=(const char*)data;
  requestMap[(uint32_t)request]+=holder.substring(0,len);
}

void handleTest(AsyncWebServerRequest *request) {
  if (request->hasArg("mqtt_addr")) {
      set_nvs("mqtt_addr", request->arg("mqtt_addr").c_str());
  }
  if (request->hasArg("mqtt_id")) {
      set_nvs("mqtt_id", request->arg("mqtt_id").c_str());
  }
  AsyncWebParameter* p = request->getParam(request->params()-1);  
  Serial.print(request->params());
  Serial.print(" post ");
  Serial.println(p->name());
  if (p->isFile()) completeFile((uint32_t)request, p->name(), p->size());
  request->send(200, "text/html", runTest());
}

void handleRoot(AsyncWebServerRequest *request) {
  String html = R"(
<html><head></head><body>
<form method="post" action='test' enctype="multipart/form-data">
 <table>
   <tr><td>Host ID<td>
   <input type="text" id="mqtt_id" name="mqtt_id" value=")";
  html += String((uint32_t)ESP.getEfuseMac(), HEX);
  html += R"(">
   <tr><td>Host Address<td>
   <input type="text" id="mqtt_addr" name="mqtt_addr" width="42">
   <tr><td>Root CA<td>
   <input type="file" id="root_cert" name="root_cert" accept=".crt,.pem">
   <tr><td>Client Certificate<td>
   <input type="file" id="client_cert" name="client_cert" accept=".crt">
   <tr><td>Private Key<td>
   <input type="file" id="cert_key" name="cert_key" accept=".key">
 </table>
   <input type='submit' value='Test Connection'/>
</form>)";
  request->send(200, "text/html", html);
}

void initWebserver() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {handleRoot(request);});
  server.on("/test", HTTP_POST,
             [](AsyncWebServerRequest *request) {handleTest(request);},
             [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) {handleUpload(request, filename, index, data, len, final);}
  );
  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {handleTest(request);});
  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404,"text/plain", "Error 404 - File not found");
  });
  server.begin();
}

void setup(){
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

#ifdef MYSSID
//  WiFi.begin(MYSSID,PASSWD);
#else
//  WiFi.begin();
#endif
while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  delay(200);
  Serial.println(WiFi.localIP());

  nvs_flash_init();
  nvs_open("mqtt", NVS_READWRITE, &nvs_mqtt);

  initWebserver();
}

void loop(){vTaskDelete(NULL);}
