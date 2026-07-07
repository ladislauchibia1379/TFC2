/*
  ============================================================================
  VERSÃO DE TESTE — SÓ ESP32 (sem DHT11, sem LCD, sem relés ligados)
  ============================================================================

  Objectivo: testar apenas a ligação WiFi e o acesso à página web,
  sem precisar de ter nenhum componente extra ligado ao ESP32.

  Os valores de temperatura/humidade e o estado da lâmpada/ventilador
  são SIMULADOS (não vêm de sensores reais), só para poderes ver a
  página a funcionar e testar os botões.

  Depois de confirmares que o WiFi e a página web funcionam bem,
  passa para o código completo (sistema_avicola_esp32.ino) e liga
  os componentes reais.
*/

#include <WiFi.h>
#include <WebServer.h>

// ---------------------- CONFIGURAÇÕES DA REDE WIFI ----------------------
const char* ssid     = "UNITEL_5G_7A9ADE";
const char* password = "3J65R3YZ8C";

// ---------------------- VARIÁVEIS SIMULADAS ----------------------
float temperatura = 28.0;
float humidade    = 60.0;

bool lampadaLigada    = false;
bool ventiladorLigado = false;

bool modoAutoLampada    = true;
bool modoAutoVentilador = true;

unsigned long ultimaAtualizacao = 0;
const unsigned long INTERVALO = 3000;

WebServer server(80);

// ============================================================================
// PÁGINA WEB (igual à versão final, para testares a interface completa)
// ============================================================================
const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fazenda Kalivala - Monitorização Avícola (TESTE)</title>
<style>
  body{font-family:Arial,Helvetica,sans-serif;background:#f2f5f0;margin:0;padding:20px;color:#2c3e2c;}
  .container{max-width:520px;margin:0 auto;}
  h1{text-align:center;font-size:1.3em;color:#2e5d32;}
  .aviso{background:#fff3cd;border:1px solid #ffe58f;padding:8px;border-radius:6px;text-align:center;font-size:0.85em;margin-bottom:14px;}
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
  <h1>Fazenda Kalivala &mdash; Monitorização Avícola</h1>
  <div class="aviso">⚠ Modo de TESTE — valores simulados (sem sensores ligados)</div>

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
// HANDLERS DO SERVIDOR WEB
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
    String dispositivo = server.arg("dispositivo");
    String acao = server.arg("acao");

    if (dispositivo == "lampada") {
      if (acao == "on")       { modoAutoLampada = false; lampadaLigada = true; }
      else if (acao == "off") { modoAutoLampada = false; lampadaLigada = false; }
      else if (acao == "auto"){ modoAutoLampada = true; }
    }
    else if (dispositivo == "ventilador") {
      if (acao == "on")       { modoAutoVentilador = false; ventiladorLigado = true; }
      else if (acao == "off") { modoAutoVentilador = false; ventiladorLigado = false; }
      else if (acao == "auto"){ modoAutoVentilador = true; }
    }
  }
  server.send(200, "text/plain", "OK");
}

// Simula pequenas variações de temperatura/humidade, só para veres os
// valores a "mexer" na página, como se fossem leituras reais
void simularLeitura() {
  temperatura += random(-10, 11) / 10.0; // varia +/-1.0
  humidade    += random(-10, 11) / 10.0;

  if (temperatura < 20) temperatura = 20;
  if (temperatura > 40) temperatura = 40;
  if (humidade < 30) humidade = 30;
  if (humidade > 90) humidade = 90;

  // Lógica automática igual à do sistema real, só para testares o comportamento
  if (modoAutoLampada)    lampadaLigada    = (temperatura < 30.0);
  if (modoAutoVentilador) ventiladorLigado = (temperatura > 34.0 || humidade > 70.0);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  Serial.println();
  Serial.println("=== MODO DE TESTE (sem sensores/LCD/relés) ===");
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
    Serial.print("Acede à página web em: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Falha ao conectar ao WiFi. Verifica o nome/senha da rede.");
  }

  server.on("/", handleRaiz);
  server.on("/dados", handleDados);
  server.on("/comando", handleComando);
  server.begin();
  Serial.println("Servidor web iniciado.");
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  server.handleClient();

  unsigned long agora = millis();
  if (agora - ultimaAtualizacao >= INTERVALO) {
    ultimaAtualizacao = agora;
    simularLeitura();
  }
}
