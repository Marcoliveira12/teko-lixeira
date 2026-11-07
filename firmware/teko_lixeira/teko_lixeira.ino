/*
 * TEKO - Lixeira Inteligente (Firmware v6 - Estavel, sonar com mediana/rajadas/passos de 5%)
 * ESP32 + HX711 (peso) + KW11-7 (tampa) + MaxSonar (nivel) + MPU6050 (movimento)
 *   + OLED SSD1306 + Buzzer (aviso de tampa) + bateria (divisor no GPIO34) + Firebase
 */

#include <Arduino.h>
#include "HX711.h"
#include <Wire.h>
#include <LittleFS.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <math.h>
#include <time.h>
#include "secrets.h"  // credenciais ficam em secrets.h (copie secrets.h.example e preencha)
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_sntp.h"

// ===================== ENUMS (Corrigir erro de prototipo do Arduino) =====================
enum NivelBateria { BAT_CARREGAR = 0, BAT_BAIXA = 1, BAT_BOA = 2, BAT_EXCELENTE = 3 };
enum EstadoWiFi { WIFI_OCIOSO, WIFI_CONECTANDO };
enum EstadoOnline { ONLINE_AGUARDANDO_HORA, ONLINE_CONFIG_FIREBASE, ONLINE_LOGIN, ONLINE_PRONTO };

// ===================== CONFIGURACOES GERAIS =====================
#define DEBUG_STATUS_SERIAL 1      // 1 = imprime o status completo no Serial; 0 = desliga
#define PESO_MINIMO 0.020          // abaixo disso, considera 0 kg
#define TIMEOUT_AFERICAO 5000      // se o peso nao travar em tanto tempo (ms), fecha a afericao com a media atual
#define MAX_FALHAS_HX711 10        // falhas seguidas ate cancelar a afericao e marcar erro
#define INTERVALO_RECONEXAO_WIFI 10000
#define TIMEOUT_TENTATIVA_WIFI 15000
#define EPOCH_VALIDO_MIN 1700000000UL  // epoch acima disso = relogio ja sincronizado (ano 2023+)

// ===================== HX711 (celula de carga) ===================
#define DT 19
#define SCK 18
#define AMOSTRAS_POR_LEITURA_HX711 1  
#define HX711_OFFSET_VAZIA -155220L
HX711 escala;

// ===================== MPU6050 (sensor de movimento) ==============
#define MPU_ADDR 0x68
#define MPU_SDA_PIN 22
#define MPU_SCL_PIN 21
#define I2C_CLOCK_HZ 400000            
bool mpuDisponivel = false;
#define INTERVALO_MPU 100              
#define INTERVALO_VERIFICACAO_MPU 500  
#define LIMIAR_MOVIMENTO 1.20          
#define LEITURAS_PARADO 8              
#define MAX_FALHAS_MPU 3
unsigned long ultimaLeituraMPU = 0;
unsigned long ultimaVerificacaoMPU = 0;
float aceleracaoAnterior = 0.0; // [CORRECAO V4] Inicia em 0 p/ pegar gravidade local no 1o ciclo
int contadorParado = 0;
int falhasConsecutivasMPU = 0;
bool lixeiraMovendo = false;

// ===================== MAXSONAR (sensor de nivel/cheia) ============
#define MAXSONAR_PWM_PIN 27         
#define NUM_LEITURAS_SONAR 25       
#define INTERVALO_LEITURA_SONAR 100 
#define TIMEOUT_SEM_PULSO_SONAR 300 
#define DISTANCIA_CHEIA_CM 16.0     
#define DISTANCIA_VAZIA_CM 75.0     // [CORRECAO V5] era 70.0; lixeira vazia = 75 cm
#define MAXSONAR_PULSO_MIN_US 100   
#define MAXSONAR_PULSO_MAX_US 30000 
#define SONAR_AQUECIMENTO_MS 3000   
// [V6] Confiabilidade do nivel (prioriza precisao sobre velocidade)
#define SONAR_ACOMODACAO_MS 1500    // espera apos a pesagem p/ o lixo assentar antes de medir
#define NUM_RAJADAS_SONAR 3         // numero de rajadas; o valor final e a mediana entre elas
#define CORTE_EXTREMOS_SONAR 5      // descarta as 5 menores e 5 maiores de cada rajada de 25
#define LIMITE_DISPERSAO_CM 6.0     // miolo da rajada mais espalhado que isso = rajada ruidosa
#define MAX_TENTATIVAS_RAJADA 2     // quantas vezes refazer rajadas ruidosas
#define PASSO_PERCENTUAL 5          // percentual de cheia so varia em multiplos de 5
#define HISTERESE_PERCENTUAL 3.0f   // so troca de degrau se o bruto passar 3 pontos do degrau atual
float resultadosRajada[NUM_RAJADAS_SONAR] = {0};
int rajadaAtual = 0;
int tentativasRajada = 0;
RTC_DATA_ATTR float distanciaBrutaCm = DISTANCIA_VAZIA_CM;
RTC_DATA_ATTR int percentualEstavel = -1;
float bufferSonar[NUM_LEITURAS_SONAR] = {0};
int indiceSonar = 0;
int quantidadeLeiturasSonar = 0;
unsigned long ultimaLeituraSonar = 0;
unsigned long ultimoPulsoRecebido = 0;
bool sonarSemSinalAvisado = false;
RTC_DATA_ATTR float distanciaAtualCm = DISTANCIA_VAZIA_CM;
RTC_DATA_ATTR bool lixeiraCheia = false;
bool sonarAtivo = false;
unsigned long sonarAquecimentoAte = 0;
bool sonarRajadaDescarte = false;

#define TIMEOUT_ESPERA_SONAR 25000  // [V6] descarte + 3 rajadas + ate 2 refeitas + acomodacao
bool registroAguardandoSonar = false;
float registroPeso = 0;
time_t registroEpoch = 0;
unsigned long registroInicioEspera = 0;
bool sonarConcluido = false;        

portMUX_TYPE sonarMux = portMUX_INITIALIZER_UNLOCKED;
volatile unsigned long isrPulsoInicio = 0;
volatile unsigned long isrUltimaDuracao = 0;
volatile bool isrNovoPulso = false;

void IRAM_ATTR isrMaxSonar() {
  if (digitalRead(MAXSONAR_PWM_PIN) == HIGH) {
    isrPulsoInicio = micros();
  } else {
    // [CORRECAO V2] Leitura de micros fora do portENTER para evitar sobrescrita por ruido
    unsigned long fim = micros();
    portENTER_CRITICAL_ISR(&sonarMux);
    isrUltimaDuracao = fim - isrPulsoInicio;
    isrNovoPulso = true;
    portEXIT_CRITICAL_ISR(&sonarMux);
  }
}

// ===================== KW11-7 (sensor de tampa) ===================
#define KW11_PIN 25
#define TAMPA_ABERTA_NIVEL LOW
#define DEBOUNCE_TAMPA_MS 100  
bool tampaAberta = false;
bool leituraBrutaTampaAnterior = false;
unsigned long momentoMudancaBrutaTampa = 0;

// ===================== ESTABILIZACAO ==============================
#define TEMPO_ESTABILIZACAO 2000  
unsigned long inicioEstabilizacao = 0;
bool estabilizando = false;

// ===================== OLED (display de status) ====================
#define TELA_LARGURA 128
#define TELA_ALTURA 64
#define OLED_RESET -1
#define ENDERECO_OLED 0x3C
#define OLED_ROTACAO 2  
#define FAIXA_AMARELA_Y 48
Adafruit_SSD1306 display(TELA_LARGURA, TELA_ALTURA, &Wire, OLED_RESET);
bool oledDisponivel = false;
#define INTERVALO_OLED 500  
unsigned long ultimaAtualizacaoOled = 0;

// ===================== BUZZER (aviso de tampa aberta) ==============
#define BUZZER_PIN 32
#define BUZZER_TEMPO_TOLERANCIA 5000   
#define BUZZER_DURACAO_BEEP 100        
#define BUZZER_TEMPO_DESISTIR 60000    
#define BUZZER_DEGRAU_1_ATE 15000      
#define BUZZER_DEGRAU_2_ATE 20000      
#define BUZZER_DEGRAU_3_ATE 35000      
#define BUZZER_INTERVALO_1 3000
#define BUZZER_INTERVALO_2 1500
#define BUZZER_INTERVALO_3 800
#define BUZZER_INTERVALO_4 400
unsigned long momentoTampaAbriu = 0;   
unsigned long ultimoBeep = 0;
unsigned long momentoBuzzerLigou = 0;
bool buzzerLigado = false;

// ===================== BATERIA (divisor de tensao no GPIO34) ========
#define BATERIA_PIN 34
#define BAT_R1 100000.0f        
#define BAT_R2 100000.0f        
#define BAT_FATOR_CAL 0.9925f    
#define BAT_AMOSTRAS 31         
#define INTERVALO_BATERIA 10000 
#define BAT_SUAVIZACAO 0.4f     

#define BAT_V_EXCELENTE 3.95f
#define BAT_V_BOA       3.75f
#define BAT_V_BAIXA     3.60f
#define BAT_V_HISTERESE 0.05f   

float bateriaTensao = 0;
NivelBateria bateriaNivel = BAT_BOA;
bool bateriaInicializada = false;
unsigned long ultimaLeituraBateria = 0;

// ===================== WI-FI =======================================
EstadoWiFi estadoWifi = WIFI_OCIOSO;
unsigned long inicioTentativaWifi = 0;
unsigned long ultimaTentativaWifi = 0;

// ===================== FIREBASE ====================================
#define FIREBASE_TIMEOUT_RESPOSTA 3000  
#define FIREBASE_TIMEOUT_CONEXAO 3000   
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

EstadoOnline estadoOnline = ONLINE_AGUARDANDO_HORA;
#define INTERVALO_TENTATIVA_ONLINE 10000
unsigned long ultimaTentativaOnline = 0;
bool ntpIniciado = false;
RTC_DATA_ATTR time_t ultimoNtp = 0;
#define INTERVALO_RESYNC_NTP 86400   // 24 h

// ===================== ENVIO FIREBASE ==============================
#define TEMPO_ENVIO_FIREBASE 5000
unsigned long ultimoEnvioFirebase = 0;
#define TEMPO_RETRY_STATUS_CHEIA 5000
RTC_DATA_ATTR bool statusPendente = false;
RTC_DATA_ATTR bool statusCheia = false;
RTC_DATA_ATTR float statusDistancia = 0;
unsigned long ultimoEnvioStatus = 0;

// ===================== LEITURA DE PESO =============================
#define NUM_LEITURAS 25         
#define INTERVALO_LEITURA 100   
float bufferLeituras[NUM_LEITURAS] = {0};
int indiceLeitura = 0;
int quantidadeLeituras = 0;
unsigned long ultimaLeitura = 0;
int falhasConsecutivasHX711 = 0;
bool hx711ComProblema = false;
bool pesoAtivo = false;
unsigned long inicioAfericao = 0;  
RTC_DATA_ATTR float pesoFinal = 0;               

// ===================== FILA OFFLINE NA FLASH (LittleFS) =============
#define FILA_MAX_REGISTROS 200
#define FILA_ARQUIVO "/fila2.bin"
#define FILA_ARQUIVO_ANTIGO "/fila.bin"  
struct RegistroPeso {
  float peso;
  uint32_t epoch;      
  float distanciaCm;   
  uint8_t cheia;       
  uint8_t nivelOk;     
  uint8_t reservado[2];
};
RegistroPeso fila[FILA_MAX_REGISTROS];
int filaQtd = 0;
bool flashOk = false;
RTC_DATA_ATTR uint32_t ultimoEpochEnviado = 0;  

// ===================== TRAVA DO PESO (anti-jitter) ==================    
#define LEITURAS_PARA_TRAVAR 5     
#define VARIACAO_DESTRAVAR 0.010   
#define CORTE_EXTREMOS_PESO 5
#define LIMITE_DISPERSAO_PESO 0.006

float mediaAnteriorPeso = 0;
int contadorEstavelPeso = 0;
bool pesoTravado = false;
float pesoAtual = 0; 
float dispersaoPeso = 0; 

// ===================== STATUS NO SERIAL ============================
#define INTERVALO_STATUS_SERIAL 3000  
unsigned long ultimoStatusSerial = 0;

// ===================== ARREDONDAMENTO ================================
float arredondar(float valor, int casas) {
  float fator = 1.0f;
  for (int i = 0; i < casas; i++) fator *= 10.0f;
  return roundf(valor * fator) / fator;
}

// [V6] Mediana aparada: ordena n leituras, descarta 'corte' de cada ponta e media o miolo.
// 'dispersao' devolve (maior - menor) do miolo, usado para rejeitar rajadas ruidosas.
float medianaAparada(float *buf, int n, int corte, float *dispersao) {
  float tmp[NUM_LEITURAS_SONAR];
  memcpy(tmp, buf, n * sizeof(float));
  for (int i = 1; i < n; i++) {
    float k = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  float soma = 0;
  for (int i = corte; i < n - corte; i++) soma += tmp[i];
  if (dispersao) *dispersao = tmp[n - corte - 1] - tmp[corte];
  return soma / (n - 2 * corte);
}

// [V6] Percentual de cheia SEMPRE em multiplos de PASSO_PERCENTUAL (5%)
int calcularPercentualCheia(float distanciaCm) {
  float pct = (DISTANCIA_VAZIA_CM - distanciaCm) * 100.0f / (DISTANCIA_VAZIA_CM - DISTANCIA_CHEIA_CM);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (int)roundf(pct / PASSO_PERCENTUAL) * PASSO_PERCENTUAL;
}

// [V6] Histerese: so troca de degrau se o valor bruto se afastar >= HISTERESE_PERCENTUAL do degrau atual.
// Devolve a distancia equivalente ao degrau estavel (assim OLED, fila e Firebase ficam consistentes).
float estabilizarDistancia(float distBrutaCm) {
  float pct = (DISTANCIA_VAZIA_CM - distBrutaCm) * 100.0f / (DISTANCIA_VAZIA_CM - DISTANCIA_CHEIA_CM);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int alvo = (int)roundf(pct / PASSO_PERCENTUAL) * PASSO_PERCENTUAL;
  if (percentualEstavel < 0 || fabsf(pct - percentualEstavel) >= HISTERESE_PERCENTUAL) {
    percentualEstavel = alvo;
  }
  return DISTANCIA_VAZIA_CM - percentualEstavel * (DISTANCIA_VAZIA_CM - DISTANCIA_CHEIA_CM) / 100.0f;
}

void limparBufferPeso() {
  indiceLeitura = 0;
  quantidadeLeituras = 0;
  pesoTravado = false;
  contadorEstavelPeso = 0;
  inicioAfericao = 0;
  mediaAnteriorPeso = 0;  // [CORRECAO V5] evita contar como "estavel" a 1a leitura apos pausa/tampa
}

bool cicloDeMedicaoEmAndamento() {
  return tampaAberta || registroAguardandoSonar || (pesoAtivo && mpuDisponivel && !lixeiraMovendo);
}

void salvarFila() {
  if (!flashOk) return;  
  File f = LittleFS.open(FILA_ARQUIVO, FILE_WRITE);
  if (!f) {
    Serial.println("[FILA] ERRO: nao consegui abrir o arquivo pra gravar.");
    return;
  }
  if (filaQtd > 0) f.write((const uint8_t*)fila, filaQtd * sizeof(RegistroPeso));
  f.close();
}

void carregarFila() {
  filaQtd = 0;
  if (!flashOk) return;
  if (LittleFS.exists(FILA_ARQUIVO_ANTIGO)) LittleFS.remove(FILA_ARQUIVO_ANTIGO);  
  if (!LittleFS.exists(FILA_ARQUIVO)) return;
  File f = LittleFS.open(FILA_ARQUIVO, FILE_READ);
  if (!f) return;
  int n = f.size() / sizeof(RegistroPeso);
  if (n > FILA_MAX_REGISTROS) n = FILA_MAX_REGISTROS;
  if (n > 0) {
    size_t lidos = f.read((uint8_t*)fila, n * sizeof(RegistroPeso));
    filaQtd = lidos / sizeof(RegistroPeso);
  }
  f.close();
  Serial.print("[FILA] Medicoes pendentes carregadas da flash: ");
  Serial.println(filaQtd);
}

void adicionarNaFila(float peso, time_t epoch, bool nivelOk, bool cheia, float distanciaCm) {
  if (filaQtd >= FILA_MAX_REGISTROS) {
    memmove(&fila[0], &fila[1], (FILA_MAX_REGISTROS - 1) * sizeof(RegistroPeso));
    filaQtd = FILA_MAX_REGISTROS - 1;
    Serial.println("[FILA] Fila cheia: medicao mais antiga descartada.");
  }
  memset(&fila[filaQtd], 0, sizeof(RegistroPeso));
  fila[filaQtd].peso = peso;
  fila[filaQtd].epoch = (epoch >= EPOCH_VALIDO_MIN) ? (uint32_t)epoch : 0;
  fila[filaQtd].nivelOk = nivelOk ? 1 : 0;
  fila[filaQtd].cheia = cheia ? 1 : 0;
  fila[filaQtd].distanciaCm = distanciaCm;
  filaQtd++;
  salvarFila();
  Serial.print("[FILA] Medicao guardada. Pendentes: ");
  Serial.print(filaQtd);
  Serial.print("/");
  Serial.println(FILA_MAX_REGISTROS);
}

void removerPrimeiroDaFila() {
  if (filaQtd <= 0) return;
  memmove(&fila[0], &fila[1], (filaQtd - 1) * sizeof(RegistroPeso));
  filaQtd--;
  salvarFila();
}

void atualizarWiFi() {
  unsigned long agora = millis();
  if (WiFi.status() == WL_CONNECTED) { estadoWifi = WIFI_OCIOSO; return; }

  switch (estadoWifi) {
    case WIFI_OCIOSO:
      if (agora - ultimaTentativaWifi >= INTERVALO_RECONEXAO_WIFI) {
        Serial.println("Iniciando conexao Wi-Fi...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        ultimaTentativaWifi = agora;
        inicioTentativaWifi = agora;
        estadoWifi = WIFI_CONECTANDO;
      }
      break;
    case WIFI_CONECTANDO:
      if (agora - inicioTentativaWifi >= TIMEOUT_TENTATIVA_WIFI) {
        Serial.println("Falha ao conectar Wi-Fi.");
        ultimaTentativaWifi = agora; // [CORRECAO V4] Resetar timer antes de desistir
        estadoWifi = WIFI_OCIOSO;
      }
      break;
  }
}

void inicializarServicosOnline() {
  if (estadoOnline == ONLINE_PRONTO) return;
  if (WiFi.status() != WL_CONNECTED) return;  

  unsigned long agora = millis();

  switch (estadoOnline) {
        case ONLINE_AGUARDANDO_HORA: {
      time_t agoraEpoch = time(nullptr);
      bool relogioValido = agoraEpoch >= EPOCH_VALIDO_MIN;
      bool sincronizou = (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
      bool precisaSync = !relogioValido || ultimoNtp == 0 ||
                         (agoraEpoch - ultimoNtp >= INTERVALO_RESYNC_NTP);
      bool ntpFalhou = ntpIniciado && relogioValido &&
                       (agora - ultimaTentativaOnline >= 8000);  // sem internet: segue assim mesmo

      if (sincronizou) ultimoNtp = agoraEpoch;

      if (relogioValido && (!precisaSync || sincronizou || ntpFalhou)) {
        Serial.print("Relogio do sistema ok. Epoch: ");
        Serial.println((unsigned long)agoraEpoch);
        estadoOnline = ONLINE_CONFIG_FIREBASE;
        ultimaTentativaOnline = agora;
        return;
      }
      if (!ntpIniciado || agora - ultimaTentativaOnline >= INTERVALO_TENTATIVA_ONLINE) {
        ntpIniciado = true;
        ultimaTentativaOnline = agora;
        Serial.println("Tentando sincronizar relogio do sistema (NTP)...");
        configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
      }
      break;
    }
    case ONLINE_CONFIG_FIREBASE: {
      config.api_key = API_KEY;
      config.database_url = DATABASE_URL;
      auth.user.email = FIREBASE_USER_EMAIL;
      auth.user.password = FIREBASE_USER_PASSWORD;
      config.token_status_callback = tokenStatusCallback;  
      config.timeout.serverResponse = FIREBASE_TIMEOUT_RESPOSTA;  
      config.timeout.socketConnection = FIREBASE_TIMEOUT_CONEXAO; 
      config.timeout.wifiReconnect = 2000;                        
      Firebase.reconnectWiFi(false);  
      estadoOnline = ONLINE_LOGIN;
      ultimaTentativaOnline = agora;
      break;
    }
    case ONLINE_LOGIN: {
      Serial.println("Autenticando no Firebase (e-mail/senha)...");
      Firebase.begin(&config, &auth);
      estadoOnline = ONLINE_PRONTO;
      break;
    }
    case ONLINE_PRONTO:
      break;
  }
}

void iniciarRajadaSonar() {
  indiceSonar = 0;
  quantidadeLeiturasSonar = 0;
  ultimoPulsoRecebido = millis();  
  sonarSemSinalAvisado = false;
  sonarConcluido = false;
  sonarAtivo = true;
  rajadaAtual = 0;                                      // [V6]
  tentativasRajada = 0;                                 // [V6]
  sonarAquecimentoAte = millis() + SONAR_ACOMODACAO_MS; // [V6] espera o lixo assentar antes de medir
  Serial.println("MaxSonar: iniciando rajada (peso ja aferido).");
}

void atualizarTampa() {
  bool leituraBruta = (digitalRead(KW11_PIN) == TAMPA_ABERTA_NIVEL);
  unsigned long agora = millis();

  if (leituraBruta != leituraBrutaTampaAnterior) {
    leituraBrutaTampaAnterior = leituraBruta;
    momentoMudancaBrutaTampa = agora;
  }

  if (leituraBruta == tampaAberta) return;  
  if (agora - momentoMudancaBrutaTampa < DEBOUNCE_TAMPA_MS) return;  

  tampaAberta = leituraBruta;
  if (tampaAberta) {
    Serial.println("Tampa: ABERTA");
    estabilizando = false;        
    sonarAtivo = false;           
    sonarConcluido = false;
    sonarRajadaDescarte = false;  
    pesoAtivo = false;            
    limparBufferPeso();           
  } else {
    Serial.println("Tampa: FECHADA");
    inicioEstabilizacao = agora;  
    estabilizando = true;
    sonarAtivo = false;
    sonarConcluido = false;
    limparBufferPeso();
    falhasConsecutivasHX711 = 0;
    pesoAtivo = true;
  }
}

void atualizarBuzzer() {
  unsigned long agora = millis();

  if (!tampaAberta) {
    if (momentoTampaAbriu != 0) {
      momentoTampaAbriu = 0;
      digitalWrite(BUZZER_PIN, LOW);
      buzzerLigado = false;
    }
    return;
  }

  if (momentoTampaAbriu == 0) {
    momentoTampaAbriu = agora;
    ultimoBeep = agora;
  }
  unsigned long tempoAberta = agora - momentoTampaAbriu;

  if (buzzerLigado) {
    if (agora - momentoBuzzerLigou >= BUZZER_DURACAO_BEEP) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerLigado = false;
    }
    return;
  }

  unsigned long intervaloAtual;
  if (tempoAberta < BUZZER_TEMPO_TOLERANCIA)        return;  
  else if (tempoAberta < BUZZER_DEGRAU_1_ATE)       intervaloAtual = BUZZER_INTERVALO_1;
  else if (tempoAberta < BUZZER_DEGRAU_2_ATE)       intervaloAtual = BUZZER_INTERVALO_2;
  else if (tempoAberta < BUZZER_DEGRAU_3_ATE)       intervaloAtual = BUZZER_INTERVALO_3;
  else if (tempoAberta < BUZZER_TEMPO_DESISTIR)     intervaloAtual = BUZZER_INTERVALO_4;
  else                                              return;  

  if (agora - ultimoBeep >= intervaloAtual) {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerLigado = true;
    momentoBuzzerLigou = agora;
    ultimoBeep = agora;
  }
}

NivelBateria classificarTensaoBateria(float v) {
  if (v >= BAT_V_EXCELENTE) return BAT_EXCELENTE;
  if (v >= BAT_V_BOA)       return BAT_BOA;
  if (v >= BAT_V_BAIXA)     return BAT_BAIXA;
  return BAT_CARREGAR;
}

NivelBateria atualizarNivelBateria(float v, NivelBateria atual) {
  NivelBateria descendo = classificarTensaoBateria(v);
  if (descendo < atual) return descendo;
  NivelBateria subindo = classificarTensaoBateria(v - BAT_V_HISTERESE);
  if (subindo > atual) return subindo;
  return atual;
}

const char* nomeNivelBateria(NivelBateria n) {
  switch (n) {
    case BAT_EXCELENTE: return "Excelente";
    case BAT_BOA:       return "Bom";
    case BAT_BAIXA:     return "Baixa";
    case BAT_CARREGAR:  return "CARREGAR";
  }
  return "?";
}

float lerBateriaMv() {
  uint32_t amostras[BAT_AMOSTRAS];
  for (int i = 0; i < BAT_AMOSTRAS; i++) {
    amostras[i] = analogReadMilliVolts(BATERIA_PIN);
    delayMicroseconds(200);
  }
  for (int i = 1; i < BAT_AMOSTRAS; i++) {
    uint32_t k = amostras[i];
    int j = i - 1;
    while (j >= 0 && amostras[j] > k) { amostras[j + 1] = amostras[j]; j--; }
    amostras[j + 1] = k;
  }
  return amostras[BAT_AMOSTRAS / 2];
}

float mvParaTensaoBateria(float mvPino) {
  return (mvPino / 1000.0f) * ((BAT_R1 + BAT_R2) / BAT_R2) * BAT_FATOR_CAL;
}

void atualizarBateria() {
  unsigned long agora = millis();
  if (bateriaInicializada && agora - ultimaLeituraBateria < INTERVALO_BATERIA) return;
  ultimaLeituraBateria = agora;

  if (!bateriaInicializada) {
    for (int i = 0; i < 10; i++) { lerBateriaMv(); delay(5); }
    float soma = 0;
    for (int i = 0; i < 10; i++) { soma += mvParaTensaoBateria(lerBateriaMv()); delay(5); }
    bateriaTensao = soma / 10.0f;
    bateriaNivel = classificarTensaoBateria(bateriaTensao);  
    bateriaInicializada = true;
    return;
  }
  float v = mvParaTensaoBateria(lerBateriaMv());
  bateriaTensao += BAT_SUAVIZACAO * (v - bateriaTensao);  
  bateriaNivel = atualizarNivelBateria(bateriaTensao, bateriaNivel);
}

#define WIFI_ICONE_W 15
#define WIFI_ICONE_H 10
const unsigned char ICONE_WIFI[] PROGMEM = {
  0x0F, 0xE0, 0x30, 0x18, 0x40, 0x04, 0x07, 0xC0, 0x08, 0x20,
  0x10, 0x10, 0x03, 0x80, 0x04, 0x40, 0x01, 0x00, 0x03, 0x80
};

void desenharIconeWiFi(bool conectado) {
  int x = TELA_LARGURA - WIFI_ICONE_W;  
  int y = 0;                            
  display.drawBitmap(x, y, ICONE_WIFI, WIFI_ICONE_W, WIFI_ICONE_H, SSD1306_WHITE);
  if (!conectado) {
    display.drawLine(x + 1, y + WIFI_ICONE_H, x + WIFI_ICONE_W, y + 1, SSD1306_BLACK);  
    display.drawLine(x, y + WIFI_ICONE_H - 1, x + WIFI_ICONE_W - 1, y, SSD1306_WHITE);  
  }
}

// [CORRECAO V3] Layout do OLED corrigido para nao vazar conteudo na tela ABERTA 
void atualizarOLED() {
  if (!oledDisponivel) return;

  unsigned long agora = millis();
  if (agora - ultimaAtualizacaoOled < INTERVALO_OLED) return;
  ultimaAtualizacaoOled = agora;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ---- FAIXA AMARELA: Titulo + Bateria/Nivel (y = 48..63) ----
  display.setTextSize(2);
  display.setCursor(0, FAIXA_AMARELA_Y);
  display.print("TEKO");

  display.setTextSize(1);
  display.setCursor(60, FAIXA_AMARELA_Y);
  display.print(nomeNivelBateria(bateriaNivel));
  
  // Percentual de cheia no lugar da tensao da bateria
  display.setCursor(60, FAIXA_AMARELA_Y + 8);
  display.print("Cheia: ");
  if (registroAguardandoSonar && !sonarConcluido) {
  display.print("...");
  } else {
  display.print(calcularPercentualCheia(distanciaAtualCm));
  display.print("%");
  }

  // ---- AREA AZUL (y = 0..47) ----
  desenharIconeWiFi(WiFi.status() == WL_CONNECTED);

  if (tampaAberta) {
    display.setTextSize(3);
    int largura = 108; // "ABERTA" (6 letras * 6px * tam 3 = 108 px)
    display.setCursor((TELA_LARGURA - largura) / 2, 12);
    display.print("ABERTA");
  } else {
    const char* mensagem = nullptr;
    String textoPeso;

    if (!mpuDisponivel) {
      mensagem = "ERRO MPU";
    } else if (lixeiraMovendo) {
      mensagem = "PAUSA";
    } else if (hx711ComProblema) {
      mensagem = "ERRO HX";
    } else if (pesoAtivo) {
      mensagem = "Medindo";
    }

    if (mensagem != nullptr) {
      display.setTextSize(2);
      int largura = (int)strlen(mensagem) * 12;
      // Centralizado nos 48px da area azul (fonte tamanho 2 = 16px de altura)
      display.setCursor((TELA_LARGURA - largura) / 2, (48 - 16) / 2);
      display.print(mensagem);
    } else {
      textoPeso = String(pesoFinal, 2) + "kg";
      
      // Tamanho 3 e o limite para caber textos como "99.99kg" na largura de 128px
      uint8_t tamanhoFonte = 3;
      display.setTextSize(tamanhoFonte);
      
      int largura = (int)textoPeso.length() * 6 * tamanhoFonte;
      int altura = 8 * tamanhoFonte;
      
      // Centralizar usando toda a area azul (48 pixels de altura)
      int yPos = (48 - altura) / 2;
      if (yPos < 0) yPos = 0;
      
      display.setCursor((TELA_LARGURA - largura) / 2, yPos);
      display.print(textoPeso);
    }
  }

  display.display();
}

bool acordarMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  
  Wire.write(0x00);  
  return (Wire.endTransmission() == 0);
}

bool verificarMPU() {
  Wire.beginTransmission(MPU_ADDR);
  return (Wire.endTransmission() == 0);
}

void verificarFalhaMPU() {
  unsigned long agora = millis();
  if (agora - ultimaVerificacaoMPU < INTERVALO_VERIFICACAO_MPU) return;
  ultimaVerificacaoMPU = agora;

  bool comunicando = verificarMPU();

  if (!comunicando) {
    falhasConsecutivasMPU++;
    Serial.print("Falha MPU: ");
    Serial.println(falhasConsecutivasMPU);

    if (falhasConsecutivasMPU >= MAX_FALHAS_MPU) {
      if (mpuDisponivel) Serial.println("ERRO: MPU6050 nao responde!");
      mpuDisponivel = false;
      lixeiraMovendo = false;
    }
  } else {
    falhasConsecutivasMPU = 0;
    if (!mpuDisponivel) {
      Serial.println("Tentando recuperar barramento I2C...");
      // [CORRECAO V4] Libera recursos I2C antes de instanciar novamente
      Wire.end();
      Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
      Wire.setClock(I2C_CLOCK_HZ);
      Wire.setTimeOut(25); // Set timeout no driver
      
      if (acordarMPU()) {
        mpuDisponivel = true;
        contadorParado = 0;
        aceleracaoAnterior = 0.0; // [CORRECAO V4] Recalibra gravidade
        inicioEstabilizacao = millis();  
        estabilizando = true;
        Serial.println("MPU6050 reinicializado!");
      }
    }
  }
}

bool lerAceleracaoMPU(float &magnitude) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  
  if (Wire.endTransmission(false) != 0) return false;

  byte recebidos = Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)6, (bool)true);
  if (recebidos != 6) return false;

  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();

  float axg = ax / 16384.0;
  float ayg = ay / 16384.0;
  float azg = az / 16384.0;
  magnitude = sqrt(axg * axg + ayg * ayg + azg * azg) * 9.81;  
  return true;
}

void atualizarMPU() {
  if (!mpuDisponivel) return;

  unsigned long agora = millis();
  if (agora - ultimaLeituraMPU < INTERVALO_MPU) return;
  ultimaLeituraMPU = agora;

  float magnitude;
  if (!lerAceleracaoMPU(magnitude)) return;  

  // [CORRECAO V4] Ignora movimento no 1o ciclo p/ assentar gravidade base do piso
  if (aceleracaoAnterior == 0.0) {
    aceleracaoAnterior = magnitude;
    return;
  }

  float variacao = fabs(magnitude - aceleracaoAnterior);
  aceleracaoAnterior = magnitude;

  if (variacao >= LIMIAR_MOVIMENTO) {
    if (!lixeiraMovendo) {
      Serial.println("MOVIMENTO DETECTADO -> PAUSA");
      limparBufferPeso();  
    }
    lixeiraMovendo = true;
    contadorParado = 0;
    estabilizando = false;
    return;
  }

  if (lixeiraMovendo) {
    contadorParado++;
    if (contadorParado >= LEITURAS_PARADO) {
      lixeiraMovendo = false;
      contadorParado = 0;
      Serial.println("Lixeira parou.");
      if (!tampaAberta) {
        inicioEstabilizacao = millis();
        estabilizando = true;
      }
    }
  }
}

void atualizarEstabilizacao() {
  if (estabilizando && !tampaAberta && !lixeiraMovendo && mpuDisponivel) {
    if (millis() - inicioEstabilizacao >= TEMPO_ESTABILIZACAO) {
      estabilizando = false;
      Serial.println("Estabilizacao concluida.");
    }
  }
}

bool atualizarLeituraPeso() {
  if (!pesoAtivo) return false;
  if (tampaAberta || lixeiraMovendo || !mpuDisponivel || estabilizando) return false;

  unsigned long agora = millis();
  if (agora - ultimaLeitura < INTERVALO_LEITURA) return false;
  ultimaLeitura = agora;

  if (escala.is_ready()) {
    float leitura = escala.get_units(AMOSTRAS_POR_LEITURA_HX711);
    if (inicioAfericao == 0) inicioAfericao = agora;
    bufferLeituras[indiceLeitura] = leitura;
    indiceLeitura = (indiceLeitura + 1) % NUM_LEITURAS;
    if (quantidadeLeituras < NUM_LEITURAS) quantidadeLeituras++;
    falhasConsecutivasHX711 = 0;
    hx711ComProblema = false;
  } else {
    falhasConsecutivasHX711++;
    if (falhasConsecutivasHX711 >= MAX_FALHAS_HX711) {
      hx711ComProblema = true;
      pesoAtivo = false;
      Serial.println("ERRO: HX711 nao responde - afericao cancelada.");
    }
    return false;
  }

  // Media aparada: descarta as 3 menores e as 3 maiores leituras e faz a media do meio
  float media;
  if (quantidadeLeituras >= NUM_LEITURAS) {
    media = medianaAparada(bufferLeituras, NUM_LEITURAS, CORTE_EXTREMOS_PESO, &dispersaoPeso);
  } else {
    float total = 0;
    for (int i = 0; i < quantidadeLeituras; i++) total += bufferLeituras[i];
    media = total / quantidadeLeituras;
    dispersaoPeso = 999;  // janela ainda nao esta cheia
  }

  if (media <= PESO_MINIMO) media = 0;

  if (pesoTravado) {
    if (fabs(media - pesoAtual) >= VARIACAO_DESTRAVAR) {
      pesoTravado = false;
      contadorEstavelPeso = 0;
      pesoAtual = media;
    }
  } else {
    pesoAtual = media;
    // So trava quando as leituras do meio estao paradas (espalhamento <= 12 g)
    if (dispersaoPeso <= LIMITE_DISPERSAO_PESO) {
      if (++contadorEstavelPeso >= LEITURAS_PARA_TRAVAR) pesoTravado = true;
    } else {
      contadorEstavelPeso = 0;
    }
  }
  mediaAnteriorPeso = media;

  return true;
}

void verificarAfericaoFinal() {
  if (!pesoAtivo) return;
  if (quantidadeLeituras < NUM_LEITURAS) return;

  bool estourouTempo = (inicioAfericao != 0) && (millis() - inicioAfericao >= TIMEOUT_AFERICAO);
  if (!pesoTravado && !estourouTempo) return;

  pesoFinal = arredondar(pesoAtual, 2);  
  pesoAtivo = false;                     

  registroPeso = pesoFinal;
  registroEpoch = time(nullptr);
  registroInicioEspera = millis();
  registroAguardandoSonar = true;
  iniciarRajadaSonar();

  Serial.print("AFERICAO FINAL: ");
  Serial.print(pesoFinal, 2);
  Serial.println(estourouTempo && !pesoTravado ? " kg (por tempo limite)" : " kg");
}

void atualizarLeituraSonar() {
  if (tampaAberta) return;
  if (!sonarAtivo) return;  

  unsigned long agora = millis();
  if (agora < sonarAquecimentoAte) {
    portENTER_CRITICAL(&sonarMux);
    isrNovoPulso = false;
    portEXIT_CRITICAL(&sonarMux);
    ultimoPulsoRecebido = agora;
    return;
  }
  if (agora - ultimaLeituraSonar < INTERVALO_LEITURA_SONAR) return;
  ultimaLeituraSonar = agora;

  bool temPulsoNovo;
  unsigned long duracao;
  portENTER_CRITICAL(&sonarMux);
  temPulsoNovo = isrNovoPulso;
  duracao = isrUltimaDuracao;
  isrNovoPulso = false;
  portEXIT_CRITICAL(&sonarMux);

  if (!temPulsoNovo) {
    if (!sonarSemSinalAvisado && agora - ultimoPulsoRecebido > TIMEOUT_SEM_PULSO_SONAR) {
      Serial.println("MaxSonar: sem sinal PWM");
      sonarSemSinalAvisado = true;
    }
    return;
  }
  ultimoPulsoRecebido = agora;
  sonarSemSinalAvisado = false;

  if (duracao < MAXSONAR_PULSO_MIN_US || duracao > MAXSONAR_PULSO_MAX_US) {
    Serial.print("MaxSonar: pulso descartado (fora da faixa): ");
    Serial.print(duracao);
    Serial.println(" us");
    return;
  }

  float leituraCm = (duracao / 147.0) * 2.54;
  
  // [CORRECAO V2] Rejeitar medicões absurdas fora do balde
  if (leituraCm > DISTANCIA_VAZIA_CM + 10.0) {
    Serial.print("MaxSonar: leitura descartada (anomalia/outlier): ");
    Serial.print(leituraCm);
    Serial.println(" cm");
    return;
  }

  bufferSonar[indiceSonar] = leituraCm;
  indiceSonar = (indiceSonar + 1) % NUM_LEITURAS_SONAR;
  if (quantidadeLeiturasSonar < NUM_LEITURAS_SONAR) quantidadeLeiturasSonar++;

  if (quantidadeLeiturasSonar < NUM_LEITURAS_SONAR) return;  

  // [V6] Mediana aparada: ordena, descarta os extremos e faz a media do miolo
  float dispersao = 0;
  float media = arredondar(medianaAparada(bufferSonar, NUM_LEITURAS_SONAR, CORTE_EXTREMOS_SONAR, &dispersao), 1);

  // [V6] Reinicia o buffer para a proxima rajada (evita janela deslizante)
  indiceSonar = 0;
  quantidadeLeiturasSonar = 0;

  if (sonarRajadaDescarte) {
    sonarRajadaDescarte = false;
    Serial.print("MaxSonar: rajada de aquecimento descartada (");
    Serial.print(media, 1);
    Serial.println(" cm). Medindo de novo.");
    return;
  }

  // [V6] Rajada ruidosa (miolo muito espalhado): descarta e refaz, ate MAX_TENTATIVAS_RAJADA vezes
  if (dispersao > LIMITE_DISPERSAO_CM && tentativasRajada < MAX_TENTATIVAS_RAJADA) {
    tentativasRajada++;
    Serial.print("MaxSonar: rajada ruidosa (dispersao ");
    Serial.print(dispersao, 1);
    Serial.print(" cm). Refazendo, tentativa ");
    Serial.println(tentativasRajada);
    return;
  }

  resultadosRajada[rajadaAtual] = media;
  rajadaAtual++;
  Serial.print("MaxSonar: rajada ");
  Serial.print(rajadaAtual);
  Serial.print("/");
  Serial.print(NUM_RAJADAS_SONAR);
  Serial.print(" = ");
  Serial.print(media, 1);
  Serial.print(" cm (dispersao ");
  Serial.print(dispersao, 1);
  Serial.println(" cm)");
  if (rajadaAtual < NUM_RAJADAS_SONAR) return;  // ainda faltam rajadas

  // [V6] Mediana entre as rajadas = valor bruto final
  float ord[NUM_RAJADAS_SONAR];
  memcpy(ord, resultadosRajada, sizeof(ord));
  for (int i = 1; i < NUM_RAJADAS_SONAR; i++) {
    float k = ord[i];
    int j = i - 1;
    while (j >= 0 && ord[j] > k) { ord[j + 1] = ord[j]; j--; }
    ord[j + 1] = k;
  }
  distanciaBrutaCm = ord[NUM_RAJADAS_SONAR / 2];

  // [V6] Quantiza em passos de 5% com histerese (valor usado no OLED/Firebase)
  distanciaAtualCm = estabilizarDistancia(distanciaBrutaCm);
  sonarAtivo = false;
  sonarConcluido = true;
  Serial.print("MaxSonar: leitura concluida (bruta ");
  Serial.print(distanciaBrutaCm, 1);
  Serial.print(" cm -> ");
  Serial.print(calcularPercentualCheia(distanciaAtualCm));
  Serial.println("%) - parado ate a proxima afericao.");
}

void atualizarNivelLixeira() {
  if (sonarAtivo) return;
  bool cheiaAgora = (distanciaAtualCm <= DISTANCIA_CHEIA_CM + 0.05f);  // [V6] tolerancia p/ float apos quantizacao em passos de 5%

  if (cheiaAgora != lixeiraCheia) {
    lixeiraCheia = cheiaAgora;
    Serial.println(lixeiraCheia ? "AVISO: lixeira CHEIA!" : "Lixeira nao esta mais cheia.");
  }
}

void finalizarRegistroComNivel() {
  if (!registroAguardandoSonar) return;

  bool estourou = (millis() - registroInicioEspera >= TIMEOUT_ESPERA_SONAR);
  if (!sonarConcluido && !tampaAberta && !estourou) return;  

  bool nivelOk = sonarConcluido;
  registroAguardandoSonar = false;
  sonarAtivo = false;  

  adicionarNaFila(registroPeso, registroEpoch, nivelOk, lixeiraCheia, distanciaAtualCm);
  ultimoEnvioFirebase = 0;  
  if (!nivelOk) Serial.println("Registro gravado sem nivel (sonar nao concluiu).");
}

bool enviarStatusCheiaFirebase(bool cheia, float distanciaCm) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (estadoOnline < ONLINE_PRONTO) return false;
  if (!Firebase.ready()) {
    Serial.println("[FB status] Firebase NAO esta pronto (nao enviado).");
    return false;
  }

  int percentual = calcularPercentualCheia(distanciaCm);
  Serial.print("[FB status] Enviando -> cheia=");
  Serial.print(cheia ? "true" : "false");
  Serial.print(" | percentualCheia=");
  Serial.print(percentual);
  Serial.println("%");

  FirebaseJson json;
  json.set("cheia", cheia);
  json.set("percentualCheia", percentual);

  bool ok = Firebase.RTDB.updateNode(&fbdo, "/lixeira/status", &json);
  Serial.print("[FB status] updateNode -> ");
  Serial.println(ok ? "OK" : ("FALHOU: " + fbdo.errorReason()));
  return ok;
}

void imprimirStatusSerial() {
#if DEBUG_STATUS_SERIAL
  unsigned long agora = millis();
  if (agora - ultimoStatusSerial < INTERVALO_STATUS_SERIAL) return;
  ultimoStatusSerial = agora;

  Serial.println("---- STATUS TEKO ----");
  Serial.print("Tampa: ");        Serial.println(tampaAberta ? "ABERTA" : "FECHADA");
  Serial.print("MPU6050: ");      Serial.println(mpuDisponivel ? "OK" : "ERRO");
  Serial.print("OLED: ");         Serial.println(oledDisponivel ? "OK" : "ERRO");
  Serial.print("Movimento: ");    Serial.println(lixeiraMovendo ? "SIM" : "NAO");
  Serial.print("Estabilizando: ");Serial.println(estabilizando ? "SIM" : "NAO");
  Serial.print("HX711: ");        Serial.println(hx711ComProblema ? "ERRO" : "OK");
  Serial.print("Afericao: ");     Serial.println(pesoAtivo ? "EM ANDAMENTO" : "AGUARDANDO TAMPA ABRIR/FECHAR");
  Serial.print("Peso final (kg): ");Serial.println(pesoFinal, 3);
  Serial.print("Peso em andamento (kg): "); Serial.println(pesoAtual, 3);
  Serial.print("Peso travado: "); Serial.println(pesoTravado ? "SIM" : "NAO");
  Serial.print("Flash (LittleFS): "); Serial.println(flashOk ? "OK" : "ERRO (fila so na RAM)");
  Serial.print("Fila offline: "); Serial.print(filaQtd); Serial.print("/"); Serial.println(FILA_MAX_REGISTROS);
  Serial.print("Registro aguardando sonar: "); Serial.println(registroAguardandoSonar ? "SIM" : "NAO");
  Serial.print("Status atual pendente: "); Serial.println(statusPendente ? "SIM" : "NAO");
  Serial.print("MaxSonar ativo: ");Serial.println(sonarAtivo ? "SIM (medindo)" : "NAO (travado)");
  Serial.print("Distancia (cm): ");Serial.println(distanciaAtualCm, 1);
  Serial.print("Distancia bruta (cm): ");Serial.println(distanciaBrutaCm, 1);
  Serial.print("Cheia (%): ");   Serial.println(calcularPercentualCheia(distanciaAtualCm));
  Serial.print("Nivel: ");        Serial.println(lixeiraCheia ? "CHEIA" : "OK");
  Serial.print("Buzzer: ");       Serial.println(momentoTampaAbriu != 0 ? "AVISANDO" : "PARADO");
  Serial.print("Bateria: ");      Serial.print(nomeNivelBateria(bateriaNivel)); Serial.print(" ("); Serial.print(bateriaTensao, 2); Serial.println(" V)");
  Serial.print("Wi-Fi: ");        Serial.println(WiFi.status() == WL_CONNECTED ? "CONECTADO" : "DESCONECTADO");
  Serial.print("Servicos online: ");
  switch (estadoOnline) {
    case ONLINE_AGUARDANDO_HORA:  Serial.println("aguardando hora (NTP)"); break;
    case ONLINE_CONFIG_FIREBASE:  Serial.println("configurando Firebase"); break;
    case ONLINE_LOGIN:            Serial.println("autenticando (login)"); break;
    case ONLINE_PRONTO:           Serial.println("PRONTO"); break;
  }
  Serial.println("----------------------");
#endif
}

bool enviarParaFirebase(float peso, time_t epoch, bool nivelOk, bool cheia, float distanciaCm) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (estadoOnline < ONLINE_PRONTO) return false;
  if (!Firebase.ready()) {
    Serial.println("[FB peso] Firebase NAO esta pronto (nao enviado, fica pendente).");
    return false;
  }

  if (epoch < EPOCH_VALIDO_MIN) epoch = time(nullptr);
  if (epoch < EPOCH_VALIDO_MIN) {
    Serial.println("[FB peso] Relogio ainda nao sincronizado (nao enviado, fica pendente).");
    return false;
  }
  if ((uint32_t)epoch <= ultimoEpochEnviado) epoch = (time_t)(ultimoEpochEnviado + 1);

  struct tm infoTempo;
  localtime_r(&epoch, &infoTempo);

  char dataStr[11];  
  strftime(dataStr, sizeof(dataStr), "%Y-%m-%d", &infoTempo);
  char horaStr[9];   
  strftime(horaStr, sizeof(horaStr), "%H:%M:%S", &infoTempo);
  String horaAtual = String(horaStr);

  unsigned long timestamp = (unsigned long)epoch;
  String caminho = "/lixeira/historico/" + String(dataStr) + "/" + String(timestamp);

  Serial.println("[FB peso] ---- ENVIANDO PARA FIREBASE ----");
  Serial.print("[FB peso] Caminho: "); Serial.println(caminho);
  Serial.print("[FB peso] Peso: ");    Serial.println(peso, 2);
  Serial.print("[FB peso] Hora: ");    Serial.println(horaAtual);

  FirebaseJson json;
  json.set("peso", peso);
  json.set("hora", horaAtual);
  
  // Enviando APENAS o status em texto da bateria (Ex: "Bom", "Excelente", "Baixa")
  json.set("bateriaNivel", nomeNivelBateria(bateriaNivel));
  Serial.print("[FB peso] Bateria Nivel: "); Serial.println(nomeNivelBateria(bateriaNivel));

  if (nivelOk) {
    int percentual = calcularPercentualCheia(distanciaCm);
    json.set("cheia", cheia);
    json.set("percentualCheia", percentual);
    Serial.print("[FB peso] Nivel: ");
    Serial.print(cheia ? "CHEIA" : "OK");
    Serial.print(" | percentualCheia=");
    Serial.print(percentual);
    Serial.println("%");
  } else {
    Serial.println("[FB peso] Nivel: indisponivel neste ciclo (so peso e hora).");
  }

  bool ok = Firebase.RTDB.setJSON(&fbdo, caminho, &json);
  Serial.print("[FB peso] setJSON -> ");
  Serial.println(ok ? "OK" : ("FALHOU: " + fbdo.errorReason()));

  if (ok) ultimoEpochEnviado = (uint32_t)epoch;
  Serial.println("[FB peso] --------------------------------");
  return ok;
}

#define TEMPO_MAX_ENVIO_ANTES_DORMIR 45000   // espera p/ enviar antes de dormir (mesmo sem Wi-Fi)
#define TEMPO_MAX_ACORDADO_FECHADA   120000  // limite geral com a tampa fechada
unsigned long inicioOcioso = 0;
unsigned long inicioTampaFechada = 0;

void dormir() {
  if (digitalRead(KW11_PIN) == TAMPA_ABERTA_NIVEL) return;  // tampa abriu agora: nao durma

  Serial.println("Dormindo (deep sleep) - acordo ao abrir a tampa.");
  Serial.flush();

  escala.power_down();
  Wire.beginTransmission(MPU_ADDR);        // MPU6050 em sleep
  Wire.write(0x6B); Wire.write(0x40);
  Wire.endTransmission();
  if (oledDisponivel) display.ssd1306_command(SSD1306_DISPLAYOFF);

  detachInterrupt(digitalPinToInterrupt(MAXSONAR_PWM_PIN));
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  digitalWrite(BUZZER_PIN, LOW);           // mantem buzzer e SCK do HX711 em nivel fixo
  gpio_hold_en((gpio_num_t)BUZZER_PIN);
  gpio_hold_en((gpio_num_t)SCK);
  gpio_deep_sleep_hold_en();

  rtc_gpio_pullup_en(GPIO_NUM_25);
  rtc_gpio_pulldown_dis(GPIO_NUM_25);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 0);  // acorda com a tampa ABERTA (LOW)
  esp_sleep_enable_timer_wakeup(6ULL * 3600ULL * 1000000ULL);
  esp_deep_sleep_start();
}

void verificarSono() {
  unsigned long agora = millis();

  if (tampaAberta) {                       // aberta: nunca dorme
    inicioOcioso = 0;
    inicioTampaFechada = 0;
    return;
  }
  if (inicioTampaFechada == 0) inicioTampaFechada = agora;

  bool medicaoConcluida = !pesoAtivo && !estabilizando &&
                          !registroAguardandoSonar && !sonarAtivo;

  if (medicaoConcluida) {
    if (inicioOcioso == 0) inicioOcioso = agora;
    if (agora - inicioOcioso >= TEMPO_MAX_ENVIO_ANTES_DORMIR) dormir();
  }

  // Rede de seguranca (MPU em erro, movimento infinito etc.)
  if (agora - inicioTampaFechada >= TEMPO_MAX_ACORDADO_FECHADA) dormir();
}


void setup() {
  gpio_hold_dis((gpio_num_t)BUZZER_PIN);
  gpio_hold_dis((gpio_num_t)SCK);
  gpio_deep_sleep_hold_dis();
  rtc_gpio_deinit(GPIO_NUM_25);

  Serial.begin(115200);
  Serial.println();
  Serial.println("Iniciando TEKO...");

  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(25); // [CORRECAO V2] Evitar deadlocks no barramento I2C

  if (acordarMPU()) {
    mpuDisponivel = true;
    Serial.println("MPU6050 inicializado!");
  } else {
    mpuDisponivel = false;
    Serial.println("ERRO: MPU6050 nao encontrado!");
    Serial.println("Sistema em modo seguro.");
  }

  if (display.begin(SSD1306_SWITCHCAPVCC, ENDERECO_OLED)) {
    oledDisponivel = true;
    display.setRotation(OLED_ROTACAO);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(3);
    display.setCursor(28, 0);   
    display.print("TEKO");
    display.setCursor(1, 24);   
    display.print("LIXEIRA");

    display.setTextSize(2);
    display.setCursor(10, FAIXA_AMARELA_Y);  
    display.print("Iniciando");

    display.display();
    Serial.println("OLED inicializado!");
  } else {
    oledDisponivel = false;
    Serial.println("ERRO: OLED nao encontrado! (seguindo sem display)");
  }

  Wire.setClock(I2C_CLOCK_HZ);

  flashOk = LittleFS.begin(true);  
  Serial.println(flashOk ? "LittleFS inicializado!" : "ERRO: LittleFS falhou! (fila so na RAM)");
  carregarFila();

  pinMode(MAXSONAR_PWM_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(MAXSONAR_PWM_PIN), isrMaxSonar, CHANGE);

  pinMode(KW11_PIN, INPUT_PULLUP);
  tampaAberta = (digitalRead(KW11_PIN) == TAMPA_ABERTA_NIVEL);
  leituraBrutaTampaAnterior = tampaAberta;
  momentoMudancaBrutaTampa = millis();
  
  sonarAtivo = false;  
  sonarRajadaDescarte = !tampaAberta;
  pesoAtivo = !tampaAberta;
  
  // [CORRECAO V2] Proteger e inicializar a estabilizacao no boot se tampa fechada
  if (!tampaAberta) {
    estabilizando = true;
    inicioEstabilizacao = millis();
  }

  Serial.print("Tampa inicial: ");
  Serial.println(tampaAberta ? "ABERTA" : "FECHADA");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  analogSetPinAttenuation(BATERIA_PIN, ADC_11db);  
  atualizarBateria();

  escala.begin(DT, SCK);
  escala.power_up();
  if (escala.wait_ready_timeout(2000)) {
    escala.set_scale(-21499.16);            
    escala.set_offset(HX711_OFFSET_VAZIA);  
    Serial.println("HX711 inicializado!");
  } else {
    hx711ComProblema = true;
    Serial.println("ERRO: HX711 nao respondeu.");
  }

  Serial.println("Iniciando conexao Wi-Fi (em segundo plano)...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  inicioTentativaWifi = millis();
  ultimaTentativaWifi = millis();
  estadoWifi = WIFI_CONECTANDO;

  sonarAquecimentoAte = millis() + SONAR_AQUECIMENTO_MS;  

  Serial.println("Setup concluido - sensores ja operando (Wi-Fi/Firebase seguem tentando em segundo plano).");
}

void loop() {
  unsigned long agora = millis();

  atualizarTampa();
  atualizarBuzzer();
  verificarFalhaMPU();
  atualizarMPU();
  atualizarEstabilizacao();
  atualizarBateria();
  atualizarOLED();
  atualizarWiFi();
  inicializarServicosOnline();  

  atualizarLeituraSonar();
  atualizarNivelLixeira();

  finalizarRegistroComNivel();

  if (statusPendente && !cicloDeMedicaoEmAndamento() && agora - ultimoEnvioStatus >= TEMPO_RETRY_STATUS_CHEIA) {
    ultimoEnvioStatus = agora;
    if (enviarStatusCheiaFirebase(statusCheia, statusDistancia)) statusPendente = false;
  }

  if (atualizarLeituraPeso()) verificarAfericaoFinal();

  if (filaQtd > 0 && !cicloDeMedicaoEmAndamento() && agora - ultimoEnvioFirebase >= TEMPO_ENVIO_FIREBASE) {
    ultimoEnvioFirebase = agora;
    RegistroPeso r = fila[0];
    if (enviarParaFirebase(r.peso, (time_t)r.epoch, r.nivelOk != 0, r.cheia != 0, r.distanciaCm)) {
      removerPrimeiroDaFila();
      if (r.nivelOk && filaQtd == 0) {
        statusCheia = (r.cheia != 0);
        statusDistancia = r.distanciaCm;
        statusPendente = true;
        ultimoEnvioStatus = 0;
      }
      ultimoEnvioFirebase = 0;
    }
  }
  verificarSono();
  imprimirStatusSerial();  
}