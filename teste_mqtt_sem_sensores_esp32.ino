
#include <WiFi.h>
#include <PubSubClient.h>


const char* ssid     = "UNITEL_5G_7A9ADE";
const char* password = "3J65R3YZ8C";

const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

const char* TOPICO_BASE = "kalivala_huambo_avicola_LADISLAU";

String topicoTemperatura      = String(TOPICO_BASE) + "/temperatura";
String topicoHumidade         = String(TOPICO_BASE) + "/humidade";
String topicoLampadaEstado    = String(TOPICO_BASE) + "/lampada/estado";
String topicoLampadaComando   = String(TOPICO_BASE) + "/lampada/comando";
String topicoLampadaModo      = String(TOPICO_BASE) + "/lampada/modo";
String topicoVentiladorEstado = String(TOPICO_BASE) + "/ventilador/estado";
String topicoVentiladorComando= String(TOPICO_BASE) + "/ventilador/comando";
String topicoVentiladorModo   = String(TOPICO_BASE) + "/ventilador/modo";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

float temperatura = 28.0;
float humidade    = 60.0;

bool lampadaLigada    = false;
bool ventiladorLigado = false;

bool modoAutoLampada    = true;
bool modoAutoVentilador = true;

unsigned long ultimaSimulacao = 0;
const unsigned long INTERVALO_SIMULACAO = 3000;

unsigned long ultimaPublicacaoMQTT = 0;
const unsigned long INTERVALO_MQTT = 5000;

void aplicarComando(String dispositivo, String acao) {
  if (dispositivo == "lampada") {
    if (acao == "on")       { modoAutoLampada = false; lampadaLigada = true; }
    else if (acao == "off") { modoAutoLampada = false; lampadaLigada = false; }
    else if (acao == "auto"){ modoAutoLampada = true; }
  } else if (dispositivo == "ventilador") {
    if (acao == "on")       { modoAutoVentilador = false; ventiladorLigado = true; }
    else if (acao == "off") { modoAutoVentilador = false; ventiladorLigado = false; }
    else if (acao == "auto"){ modoAutoVentilador = true; }
  }
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String mensagem;
  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  mensagem.trim();
  mensagem.toLowerCase();

  String topicoRecebido = String(topic);

  Serial.print("Mensagem recebida [");
  Serial.print(topicoRecebido);
  Serial.print("]: ");
  Serial.println(mensagem);

  if (topicoRecebido == topicoLampadaComando) {
    aplicarComando("lampada", mensagem);
  } else if (topicoRecebido == topicoVentiladorComando) {
    aplicarComando("ventilador", mensagem);
  }
}

void reconectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("A ligar ao broker MQTT...");
    String clientId = "ESP32-Teste-" + WiFi.macAddress();

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" ligado!");
      mqttClient.subscribe(topicoLampadaComando.c_str());
      mqttClient.subscribe(topicoVentiladorComando.c_str());
      Serial.println("Subscrito aos tópicos de comando.");
    } else {
      Serial.print(" falhou, código=");
      Serial.print(mqttClient.state());
      Serial.println(" — nova tentativa em 3s");
      delay(3000);
    }
  }
}

void publicarEstadoMQTT() {
  mqttClient.publish(topicoTemperatura.c_str(), String(temperatura, 1).c_str());
  mqttClient.publish(topicoHumidade.c_str(), String(humidade, 1).c_str());
  mqttClient.publish(topicoLampadaEstado.c_str(), lampadaLigada ? "on" : "off");
  mqttClient.publish(topicoVentiladorEstado.c_str(), ventiladorLigado ? "on" : "off");
  mqttClient.publish(topicoLampadaModo.c_str(), modoAutoLampada ? "auto" : "manual");
  mqttClient.publish(topicoVentiladorModo.c_str(), modoAutoVentilador ? "auto" : "manual");

  Serial.println("--- Estado publicado no broker ---");
  Serial.print("Temp: "); Serial.print(temperatura);
  Serial.print(" | Hum: "); Serial.print(humidade);
  Serial.print(" | Lampada: "); Serial.print(lampadaLigada ? "ON" : "OFF");
  Serial.print(" (" ); Serial.print(modoAutoLampada ? "auto" : "manual"); Serial.print(")");
  Serial.print(" | Ventilador: "); Serial.print(ventiladorLigado ? "ON" : "OFF");
  Serial.print(" (" ); Serial.print(modoAutoVentilador ? "auto" : "manual"); Serial.println(")");
}

void simularLeitura() {
  temperatura += random(-10, 11) / 10.0;
  humidade    += random(-10, 11) / 10.0;

  if (temperatura < 20) temperatura = 20;
  if (temperatura > 40) temperatura = 40;
  if (humidade < 30) humidade = 30;
  if (humidade > 90) humidade = 90;

  if (modoAutoLampada)    lampadaLigada    = (temperatura < 30.0);
  if (modoAutoVentilador) ventiladorLigado = (temperatura > 34.0 || humidade > 70.0);
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  Serial.println();
  Serial.println("=== MODO DE TESTE — ESP32 + MQTT (sem sensores/LCD/relés) ===");
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado com sucesso!");
    Serial.print("IP local: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Falha ao conectar ao WiFi. Verifica o nome/senha da rede.");
  }

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callbackMQTT);

  Serial.print("Prefixo dos tópicos MQTT: ");
  Serial.println(TOPICO_BASE);
  Serial.println("Abre o painel_remoto_mqtt.html (com o mesmo prefixo) para testar.");
}

void loop() {
  if (!mqttClient.connected()) {
    reconectarMQTT();
  }
  mqttClient.loop();

  unsigned long agora = millis();

  if (agora - ultimaSimulacao >= INTERVALO_SIMULACAO) {
    ultimaSimulacao = agora;
    simularLeitura();
  }

  if (agora - ultimaPublicacaoMQTT >= INTERVALO_MQTT) {
    ultimaPublicacaoMQTT = agora;
    publicarEstadoMQTT();
  }
}
