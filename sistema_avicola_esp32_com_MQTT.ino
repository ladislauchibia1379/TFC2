/*
  ============================================================================
  SISTEMA AUTOMÁTICO DE CONTROLO E MONITORIZAÇÃO REMOTA DE CRIADEIRAS AVÍCOLAS
  Fazenda Kalivala - Huambo
  VERSÃO COM COMUNICAÇÃO REMOTA (MQTT) — acesso fora da rede local
  ============================================================================

  Esta versão mantém tudo o que já tínhamos (WiFi, LCD, DHT11, página web
  local, controlo automático e manual por relés) e ADICIONA comunicação
  via protocolo MQTT, para que o dispositivo possa ser monitorizado e
  controlado a partir de QUALQUER rede com internet (não só a rede local).

  Como funciona:
   - O ESP32 liga-se a um "broker" MQTT (um servidor intermediário na
     internet) e PUBLICA os valores de temperatura, humidade e estado
     dos dispositivos.
   - O ESP32 também SUBSCREVE (fica "à escuta") em tópicos de comando,
     para poder ser controlado remotamente (ligar/desligar/automático).
   - Uma página web separada (remote_dashboard.html), aberta em qualquer
     dispositivo com internet, liga-se ao MESMO broker e comunica com
     o ESP32 através dele — não precisa de estar na mesma rede WiFi.

  IMPORTANTE — Broker público vs. privado:
   Este código usa, por defeito, o broker público "broker.hivemq.com"
   (gratuito, sem necessidade de conta, ideal para testes/desenvolvimento).
   Por ser público, qualquer pessoa que souber os nomes dos teus tópicos
   pode ver os dados ou enviar comandos. Por isso:
     1. Usamos um PREFIXO ÚNICO nos tópicos (definido abaixo em TOPICO_BASE)
        para reduzir a hipótese de colisão com outros utilizadores;
     2. Para uma versão final / apresentação do trabalho, recomenda-se
        migrar para um broker privado com autenticação, por exemplo:
        - HiveMQ Cloud (tem plano gratuito, com utilizador/senha e TLS)
        - Mosquitto próprio, num servidor/Raspberry Pi
     Este ponto pode e deve ser discutido no capítulo de "Trabalhos
     Futuros" ou "Limitações" do teu trabalho escrito.

  Biblioteca adicional necessária:
   - PubSubClient (by Nick O'Leary) — instalar via Library Manager
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>

// ---------------------- CONFIGURAÇÕES DA REDE WIFI ----------------------
const char* ssid     = "NOME_DA_SUA_REDE";
const char* password = "SENHA_DA_SUA_REDE";

// ---------------------- CONFIGURAÇÃO DO BROKER MQTT ----------------------
const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

// Prefixo único dos tópicos — ALTERA isto para algo só teu, ex: o teu nome
// ou número de estudante, para reduzir a hipótese de colisão com outros.
const char* TOPICO_BASE = "kalivala_huambo_avicola_SEUNOME";

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

// ---------------------- CONFIGURAÇÃO DO SENSOR DHT11 ----------------------
#define DHTPIN   4
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

// ---------------------- CONFIGURAÇÃO DO LCD I2C ----------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------------- PINOS DOS RELÉS ----------------------
#define RELE_LAMPADA    26
#define RELE_VENTILADOR 27
#define RELE_ON  LOW
#define RELE_OFF HIGH

// ---------------------- LIMIARES PARA PINTINHOS ----------------------
float TEMP_MIN = 30.0;
float TEMP_MAX = 34.0;
float HUM_MAX  = 70.0;

// ---------------------- VARIÁVEIS DE ESTADO ----------------------
float temperatura = 0.0;
float humidade    = 0.0;

bool lampadaLigada    = false;
bool ventiladorLigado = false;

bool modoAutoLampada    = true;
bool modoAutoVentilador = true;

unsigned long ultimaLeitura = 0;
const unsigned long INTERVALO_LEITURA = 3000;

unsigned long ultimaPublicacaoMQTT = 0;
const unsigned long INTERVALO_MQTT = 5000; // publica no broker a cada 5s

WebServer server(80);

// ============================================================================
// PÁGINA WEB LOCAL (igual à versão anterior — continua disponível na rede local)
// ============================================================================
const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fazenda Kalivala - Monitorização Avícola</title>
<style>
  body{font-family:Arial,Helvetica,sans-serif;background:#f2f5f0;margin:0;padding:20px;color:#2c3e2c;}
  .container{max-width:520px;margin:0 auto;}
  h1{text-align:center;font-size:1.4em;color:#2e5d32;}
  .card{background:#fff;border-radius:10px;padding:16px;margin-bottom:16px;box-shadow:0 2px 6px rgba(0,0,0,0.1);}
  .valores{display:flex;justify-content:space-around;text-align:center;}
  .valor{font-size:1.8em;font-weight:bold;}
  .label{font-size:0.85em;color:#666;}
  .dispositivo{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;}
  .estado{font-weight:bold;padding:4px 10px;border-radius:6px;color:#fff;}
  .on{background:#2e7d32;}
  .off{background:#b0bec5;}
  button{padding:8px 14px;margin-left:6px;border:none;border-radius:6px;cursor:pointer;font-size:0.9em;color:#fff;}
  .btn-on{background:#2e7d32;}
  .btn-off{background:#c62828;}
  .btn-auto{background:#1565c0;}
  .modo{font-size:0.8em;color:#555;margin-top:4px;}
</style>
</head>
<body>
<div class="container">
  <h1>Fazenda Kalivala &mdash; Monitorização Avícola (Local)</h1>

  <div class="card">
    <div class="valores">
      <div>
        <div class="valor" id="temp">--</div>
        <div class="label">Temperatura (&deg;C)</div>
      </div>
      <div>
        <div class="valor" id="hum">--</div>
        <div class="label">Humidade (%)</div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="dispositivo">
      <span>💡 Lâmpada (aquecimento)</span>
      <span id="estadoLampada" class="estado off">--</span>
    </div>
    <div>
      <button class="btn-on" onclick="comando('lampada','on')">Ligar</button>
      <button class="btn-off" onclick="comando('lampada','off')">Desligar</button>
      <button class="btn-auto" onclick="comando('lampada','auto')">Automático</button>
    </div>
    <div class="modo" id="modoLampada">Modo: --</div>
  </div>

  <div class="card">
    <div class="dispositivo">
      <span>🌀 Ventilador</span>
      <span id="estadoVentilador" class="estado off">--</span>
    </div>
    <div>
      <button class="btn-on" onclick="comando('ventilador','on')">Ligar</button>
      <button class="btn-off" onclick="comando('ventilador','off')">Desligar</button>
      <button class="btn-auto" onclick="comando('ventilador','auto')">Automático</button>
    </div>
    <div class="modo" id="modoVentilador">Modo: --</div>
  </div>

</div>

<script>
async function atualizarDados(){
  try{
    const resp = await fetch('/dados');
    const d = await resp.json();
    document.getElementById('temp').innerText = d.temperatura.toFixed(1);
    document.getElementById('hum').innerText = d.humidade.toFixed(1);

    const estL = document.getElementById('estadoLampada');
    estL.innerText = d.lampada ? 'LIGADA' : 'DESLIGADA';
    estL.className = 'estado ' + (d.lampada ? 'on' : 'off');
    document.getElementById('modoLampada').innerText = 'Modo: ' + (d.autoLampada ? 'Automático' : 'Manual');

    const estV = document.getElementById('estadoVentilador');
    estV.innerText = d.ventilador ? 'LIGADO' : 'DESLIGADO';
    estV.className = 'estado ' + (d.ventilador ? 'on' : 'off');
    document.getElementById('modoVentilador').innerText = 'Modo: ' + (d.autoVentilador ? 'Automático' : 'Manual');
  }catch(e){
    console.error('Erro ao obter dados', e);
  }
}

async function comando(dispositivo, acao){
  await fetch('/comando?dispositivo=' + dispositivo + '&acao=' + acao);
  atualizarDados();
}

setInterval(atualizarDados, 2000);
window.onload = atualizarDados;
</script>
</body>
</html>
)rawliteral";

// ============================================================================
// FUNÇÕES DE CONTROLO DOS RELÉS
// ============================================================================
void ligarLampada(bool estado) {
  lampadaLigada = estado;
  digitalWrite(RELE_LAMPADA, estado ? RELE_ON : RELE_OFF);
}

void ligarVentilador(bool estado) {
  ventiladorLigado = estado;
  digitalWrite(RELE_VENTILADOR, estado ? RELE_ON : RELE_OFF);
}

// ============================================================================
// LÓGICA DE CONTROLO AUTOMÁTICO
// ============================================================================
void controloAutomatico() {
  if (modoAutoLampada) {
    if (temperatura < TEMP_MIN) {
      ligarLampada(true);
    } else if (temperatura >= TEMP_MIN + 1.0) {
      ligarLampada(false);
    }
  }

  if (modoAutoVentilador) {
    if (temperatura > TEMP_MAX || humidade > HUM_MAX) {
      ligarVentilador(true);
    } else if (temperatura <= TEMP_MAX - 1.0 && humidade <= HUM_MAX - 5.0) {
      ligarVentilador(false);
    }
  }
}

// ============================================================================
// COMUNICAÇÃO MQTT
// ============================================================================

// Aplica um comando recebido (via MQTT ou via web local) — função partilhada
void aplicarComando(String dispositivo, String acao) {
  if (dispositivo == "lampada") {
    if (acao == "on")       { modoAutoLampada = false; ligarLampada(true); }
    else if (acao == "off") { modoAutoLampada = false; ligarLampada(false); }
    else if (acao == "auto"){ modoAutoLampada = true; }
  } else if (dispositivo == "ventilador") {
    if (acao == "on")       { modoAutoVentilador = false; ligarVentilador(true); }
    else if (acao == "off") { modoAutoVentilador = false; ligarVentilador(false); }
    else if (acao == "auto"){ modoAutoVentilador = true; }
  }
}

// Chamada sempre que chega uma mensagem MQTT num tópico subscrito
void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  String mensagem;
  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  mensagem.trim();
  mensagem.toLowerCase();

  String topicoRecebido = String(topic);

  if (topicoRecebido == topicoLampadaComando) {
    aplicarComando("lampada", mensagem);
  } else if (topicoRecebido == topicoVentiladorComando) {
    aplicarComando("ventilador", mensagem);
  }
}

// Liga (ou religa) ao broker MQTT, e subscreve aos tópicos de comando
void reconectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("A ligar ao broker MQTT...");
    // ID de cliente único, baseado no MAC do ESP32, para evitar conflitos
    String clientId = "ESP32-Kalivala-" + WiFi.macAddress();

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" ligado!");
      mqttClient.subscribe(topicoLampadaComando.c_str());
      mqttClient.subscribe(topicoVentiladorComando.c_str());
    } else {
      Serial.print(" falhou, código=");
      Serial.print(mqttClient.state());
      Serial.println(" — nova tentativa em 3s");
      delay(3000);
    }
  }
}

// Publica o estado actual no broker, para quem estiver a ver remotamente
void publicarEstadoMQTT() {
  mqttClient.publish(topicoTemperatura.c_str(), String(temperatura, 1).c_str());
  mqttClient.publish(topicoHumidade.c_str(), String(humidade, 1).c_str());
  mqttClient.publish(topicoLampadaEstado.c_str(), lampadaLigada ? "on" : "off");
  mqttClient.publish(topicoVentiladorEstado.c_str(), ventiladorLigado ? "on" : "off");
  mqttClient.publish(topicoLampadaModo.c_str(), modoAutoLampada ? "auto" : "manual");
  mqttClient.publish(topicoVentiladorModo.c_str(), modoAutoVentilador ? "auto" : "manual");
}

// ============================================================================
// LEITURA DO SENSOR E ATUALIZAÇÃO DO LCD
// ============================================================================
void lerSensorEAtualizarLCD() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro no sensor");
    lcd.setCursor(0, 1);
    lcd.print("DHT11!");
    return;
  }

  temperatura = t;
  humidade = h;

  controloAutomatico();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(temperatura, 1);
  lcd.print((char)223);
  lcd.print("C");

  lcd.setCursor(0, 1);
  lcd.print("Hum: ");
  lcd.print(humidade, 1);
  lcd.print("%");
}

// ============================================================================
// HANDLERS DO SERVIDOR WEB LOCAL
// ============================================================================
void handleRaiz() {
  server.send_P(200, "text/html", PAGINA_HTML);
}

void handleDados() {
  String json = "{";
  json += "\"temperatura\":" + String(temperatura, 1) + ",";
  json += "\"humidade\":" + String(humidade, 1) + ",";
  json += "\"lampada\":" + String(lampadaLigada ? "true" : "false") + ",";
  json += "\"ventilador\":" + String(ventiladorLigado ? "true" : "false") + ",";
  json += "\"autoLampada\":" + String(modoAutoLampada ? "true" : "false") + ",";
  json += "\"autoVentilador\":" + String(modoAutoVentilador ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleComando() {
  if (server.hasArg("dispositivo") && server.hasArg("acao")) {
    aplicarComando(server.arg("dispositivo"), server.arg("acao"));
  }
  server.send(200, "text/plain", "OK");
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  pinMode(RELE_LAMPADA, OUTPUT);
  pinMode(RELE_VENTILADOR, OUTPUT);
  ligarLampada(false);
  ligarVentilador(false);

  lcd.init();
  lcd.backlight();
  dht.begin();

  // ---------- LIGAÇÃO AO WIFI ----------
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando...");
  lcd.setCursor(0, 1);
  lcd.print(ssid);

  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi conectado!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    Serial.println();
    Serial.print("IP do ESP32 (rede local): ");
    Serial.println(WiFi.localIP());
    delay(3000);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Falha na ligacao");
    lcd.setCursor(0, 1);
    lcd.print("Verifique WiFi");
    delay(3000);
  }

  // ---------- MQTT (COMUNICAÇÃO REMOTA) ----------
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callbackMQTT);

  // ---------- SERVIDOR WEB LOCAL ----------
  server.on("/", handleRaiz);
  server.on("/dados", handleDados);
  server.on("/comando", handleComando);
  server.begin();
  Serial.println("Servidor web local iniciado.");
  Serial.println("Comunicação remota (MQTT) configurada.");
  Serial.print("Prefixo dos tópicos: ");
  Serial.println(TOPICO_BASE);
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  // Mantém a ligação MQTT viva (reconecta se cair)
  if (!mqttClient.connected()) {
    reconectarMQTT();
  }
  mqttClient.loop();

  // Servidor web local (rede local)
  server.handleClient();

  unsigned long agora = millis();

  // Leitura do sensor e actualização do LCD
  if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = agora;
    lerSensorEAtualizarLCD();
  }

  // Publicação periódica no broker MQTT (comunicação remota)
  if (agora - ultimaPublicacaoMQTT >= INTERVALO_MQTT) {
    ultimaPublicacaoMQTT = agora;
    publicarEstadoMQTT();
  }
}
