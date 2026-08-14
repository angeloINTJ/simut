//versao do software
#define software "Banho p/ Calibr."
#define versao "v1.7.1"

//seletor com triplo passo na função temp_set()
//pisca o número da casa selecionada
//resolução erro PWM
//resolução set decimal
//-10graus
//_vento
//grava_set
//pisca numero parado na transição
//media e estabilização otimizadas
//tela de ajuste manual
//controle apromorado nas baixas temperaturas
//resolvido erro configurações


// bibliotecas
#include <LiquidCrystal.h> //Biblioteca LCD
#include <DHT.h>           //biblioteca sensor DHT
#include <OneWire.h>       //biblioteca barramento de um fio
#include <EEPROM.h>        //biblioteca EEPROM

// definições de dispositivos
LiquidCrystal lcd(8, //
                  7,
                  6,
                  5,
                  4,
                  3
                  );   // pinagem do LCD


static byte dhtsn1[] = {0x01, 0x70, 0x33, 0x2E, 0x99};

#define _aquece 13
#define _esfria 12
#define _pwm    11
#define _vento  9
#define _power  A3

#define _contr  10
//#define _brilh  // falta saída pwm                  

#define _18b20  A1     // pino do sensor de temperatura
#define _dht1   A0     // pino do sensor DHT

DHT dht(_dht1, DHT22); // pino e tipo do sensor DHT

OneWire ds(_18b20);    // pino do sensor de temperatura

// variáveis de configuração
unsigned short contr  = 38;
unsigned short scontr = 0; // contraste da tela
                 

// botões/knobs
#define _btn  A5
#define btn digitalRead(_btn)
#define _knob A2
#define knob analogRead(_knob)

// protótipo de funções
void estabiliza (void);
int  pid_proc   (void);
void display_set(unsigned int);
void lerdht     (void);
void ler18b20   (void);
void numerao    (int);
void controle   (int);
void config_disp(unsigned short);
void temp_set   (void);
void temp_set_passo   (void);
void grava_set  (void);
float media_movel(void);

// variáveis globais
byte display_atual, // tela a ser exibida
          retencao,      // informa que o botão permaneceu pressionado quando em 1
        novidade_h,      // informa que há algo novo a ser apresentado no display
        novidade_t,
      novidade_set,
      novidade_pwm,
     novidade_temp,
     novidade_page,
       novidade_n1,
       novidade_n2,
       novidade_n3 = 0;
       
       float deslig = 0;
       float media_temp[10] = {0};
       float disp_media = 0;
       float amostra_temp = 0;



// variaveis PID
double error;
double ant_celsius;
double kP = 75.00,
       kI = 3.30,
       kD = 1.00;     
double skP = kP;
double skI = kI;
double skD = kD;  
double P, I, D;
int    pid_contr, antpwm = 0;
  
long ant_Process;

int anttemp, antset, set, knob_set, knob_antset, knob_pos, knob_antpos = 0;
float anttemp_amb, antumi_amb = 0;
unsigned int tempo_btn, config_page = 0;
unsigned short antknob = 0;

unsigned int pisca_numerao = 0;
unsigned int force_numerao = 0;
byte         set_dezena = 0;

// Variáveis auxiliares para 18b20
byte  present;        //Armazena a rom do sensor presente no barramento
byte  type_s;         //Armazena do tipo do sensor
byte  data[12];       //Armazena os dados enviados pelo sensor
byte  addr[8];        //Armazena a ROM do sensor atual
byte  ds_novo;

byte  fim_scan;       //Flag que identifica o fim do escaneamento.
byte num_sensor = 9;  //número de sensores programados

float celsius;        //Armazena a temperatura já convertida

byte  err[8];         //Armazena o estado de erro
int   s_sensor[8][8]; //Armazena o número se série de todos os sensores
float t, h;           //Armazena a temperatura e a umidade do DHT
float sensort[8];     //Armazena a temperatura indempendente de cada sensor

byte  dhtteste;       //FLAG que indica se o DHT está presente
int   k;
int   luz = 100;
int   iniciando = 50;

int numero, tempant1,tempant2, tempant3;
int x = 0;


//Arrays para criação dos segmentos e customização dos números
byte LT[8] = 
{
  B00111,  B01111,  B11111,  B11111,  B11111,  B11111,  B11111,  B11111
};
byte UB[8] =
{
  B11111,  B11111,  B00000,  B00000,  B00000,  B00000,  B00000,  B00000
};
byte RT[8] =
{
  B11100,  B11110,  B11111,  B11111,  B11111,  B11111,  B11111,  B11111
};
byte LL[8] =
{
  B11111,  B11111,  B11111,  B11111,  B11111,  B11111,  B01111,  B00111
};
byte LB[8] =
{
  B00000,  B00000,  B00000,  B00000,  B00000,  B00000,  B11111,  B11111
};
byte LR[8] =
{
  B11111,  B11111,  B11111,  B11111,  B11111,  B11111,  B11110,  B11100
};
byte UMB[8] =
{
  B11111,  B11111,  B00000,  B00000,  B00000,  B00000,  B11111,  B11111
};
byte GR[8] =
{
  B01100,  B10010,  B10010,  B01100,  B00000,  B00000,  B00000,  B00000
};

void setup() {

  Serial.begin(9600);
  Serial.print("Serial Iniciada");
  
  pinMode(_btn,INPUT_PULLUP);
  pinMode(_knob,      INPUT);
  pinMode(_contr,    OUTPUT);
  pinMode(_aquece,   OUTPUT);
  pinMode(_esfria,   OUTPUT);
  pinMode(_pwm,      OUTPUT);
  pinMode(_vento,    OUTPUT);
  pinMode(_power,    OUTPUT);
  analogWrite(_vento,    80);
  digitalWrite(_power, HIGH);

  dht.begin();           // Iniclaiza o sensor DHT

  lcd.begin(16, 2); //Inicializa o LCD
   
  lcd.createChar(0,LT);  // Associa cada segmento criado, a um número
  lcd.createChar(1,UB);
  lcd.createChar(2,RT);
  lcd.createChar(3,LL);
  lcd.createChar(4,LB);
  lcd.createChar(5,LR);
  lcd.createChar(6,UMB);
  lcd.createChar(7,GR);

  if(EEPROM.read(0) == 0)
  {
    contr = EEPROM.read(1);
    kP = (unsigned short)EEPROM.read(2);
    kI = (float)(EEPROM.read(3))/50;
    kD = (float)(EEPROM.read(4))/50;
    

    scontr = contr;
    skP = kP;       
    skI = kI;     
    skD = kD;

  }

  analogWrite(_contr, contr);

  

  lcd.setCursor(0,0);
  lcd.print(software);
  lcd.setCursor(0,1);
  lcd.print(versao);

  delay(1500);

  lcd.clear();
  
  if(!btn) 
  {  
    lcd.setCursor(3,0);
    lcd.print("INICIANDO");
    lcd.setCursor(1,1);
    lcd.print("CONFIGURACOES");
    while(!btn);
    lcd.clear();
    display_set(3);
  }

  novidade_set = 1;
  knob_pos = map(knob, 0, 1023, -9, 9);
  knob_antpos = knob_pos;
  
  if(EEPROM.read(7) != 255)
  {
    set = (EEPROM.read(7)) << 8;
    set = set | (EEPROM.read(6));
    display_atual = 0;            // inicia o display na tela principal
  }
  else
  {
    temp_set_passo();
    grava_set();
  }
  
  estabiliza();
  novidade_set  = 1;
  novidade_pwm  = 1;
  novidade_temp = 1;
  novidade_page = 1;
  retencao = 1;
}

void loop() {

  
  if(!retencao && !btn)
  {
    lcd.clear();
    display_atual++;
    novidade_h    = 1;      // informa que há algo novo a ser apresentado no display
    novidade_t    = 1;
    novidade_set  = 1;
    novidade_pwm  = 1;
    novidade_temp = 1;
    novidade_page = 1;
    retencao = 1;
  }
  if(display_atual > 2) 
  {
    knob_antpos = map(knob, 0, 1023, -9, 9);
    display_atual = 0;
  }
  if(novidade_page){
    display_set(display_atual);
  }

  lerdht();
  if(anttemp_amb != t)
  {
    novidade_t = 1;
    anttemp_amb = t;
  }
  if(antumi_amb != h)
  {
    novidade_h = 1;
    antumi_amb = h;
  }
  
  ler18b20();
  if(anttemp != (int)(celsius*10))
  {
    if(celsius < 80.0)
    {
      novidade_temp = 1;
      anttemp = celsius*10;
    }
  }
  

  if(display_atual < 2) knob_pos = map(knob, 0, 1023, -9, 9);
  else knob_pos = map(knob, 0, 1023, -255, 255);
  
  if(knob_antpos != knob_pos)
  {
    if((abs(knob_antpos - knob_pos)) > 1)
    {
      novidade_set = 1;
      if(display_atual < 2)
      {
        temp_set_passo();
        grava_set();
        novidade_temp = 1;
      }
      knob_antpos = knob_pos;
      deslig = millis();
    }
  }  
    
  if(display_atual < 2) pid_contr = pid_proc();
  else pid_contr = knob_pos;

  if(antpwm != pid_contr)
  {
    novidade_pwm = 1;
    antpwm = pid_contr;
  }
  
  controle(pid_contr);

  Serial.print(celsius);
  Serial.print(" ");
  Serial.print(set);
  Serial.print(" ");
  Serial.print(map(pid_contr, -255, 255, -100, 100));
  Serial.print(" ");
  Serial.print(P);
  Serial.print(" ");
  Serial.print(I);
  Serial.print(" ");
  Serial.println(D);

  delay(50);
  if(btn) 
  {
    retencao = 0;
    deslig = millis();
  }
  
  if(((millis()- deslig) > 3000) || (celsius > 65.0)) 
  {

    if(celsius > 65.0)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("    PROTECAO    ");
      lcd.setCursor(0, 1);
      lcd.print("SUPERAQUECIMENTO");

      analogWrite (_pwm, 0);
      digitalWrite(_esfria, LOW);
      digitalWrite(_aquece, LOW); 
      analogWrite (_vento, 255);

      delay(1000);
      lcd.setCursor(0, 1);
      lcd.print("   DESLIGANDO   ");

      delay(1000);
    }
    else{
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("   DESLIGANDO   ");

      delay(1000);
    }
    
    EEPROM.write(7, 0xFF);
    
    digitalWrite(_power, LOW);
  }

  if((millis() - amostra_temp) > 750)
  {
    disp_media = media_movel();
    amostra_temp = millis();
  }
}

void estabiliza()
{
  int i = 0;
  lerdht();
  ler18b20();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(" ESTABILIZANDO ");
  lcd.setCursor(0,1);
  lcd.print("    SENSORES   ");
  for(i = 0; i < 10; i++)
  {
    delay(760);
    ler18b20();
    media_movel();
  }
  novidade_pwm  = 1;
  novidade_set  = 1;
  novidade_temp = 1;
  lcd.clear();
}

int pid_proc()
{
  int pid;
  float aux;
  
  // Implementação P ID
  error = (float)set/10 - celsius;
  float deltaTime = (millis() - ant_Process) / 3000.0;
  ant_Process = millis();
    
  //P
  //I

  if(set > 100)
  {
    P = error * kP;
    I = I + (error * kI) * deltaTime;
  }
  else
  {
    aux = set;
    P = error * map(aux, -110, 100, 25000, kP);
    P /= 10;
    if(P > 0) P = 0;

    I = I + (error * kI) * deltaTime;
    if(I > 0) I = 0;
  }
   
  //D
  D = (ant_celsius - celsius) * kD / deltaTime;
  ant_celsius = celsius;
    
  // Soma tudo
  pid = P + I + D;
  if(pid >  255) pid =  255;
  if(pid < -255) pid = -255;
    
  return pid;
}

void display_set(unsigned int tab){
 
  unsigned short ok = 0;
  unsigned int   tempo = 0;
  unsigned short i;
  
  switch(tab){
    case 0:
    {
      if(novidade_set)
      {
        lcd.setCursor(0,1);
        lcd.print("      ");
        lcd.setCursor(0,1);
        
        lcd.print((float)set/10);
        if(set  == -100)       lcd.setCursor(3,1);
        if((-100<set)&&(set<0))lcd.setCursor(4,1);
        if((-1<set)&&(set<100))lcd.setCursor(3,1);
        if(99<set)             lcd.setCursor(4,1);
        
        lcd.write(7);
        lcd.write("C");
        lcd.write(" ");
        novidade_set = 0;
      }
      if(novidade_temp)
      {
        numerao(disp_media *10);
        lcd.setCursor(12,1);
        lcd.print(",");
        novidade_temp = 0;
      }
      
      if(novidade_pwm)
      {
        lcd.setCursor(0,0);
        lcd.print("     ");
        lcd.setCursor(0,0);
        if((map(pid_contr, -255, 255, -100, 100)) > 0) lcd.print("+");
        lcd.print(map(pid_contr, -255, 255, -100, 100));
        lcd.print("%");
        novidade_pwm = 0;
      }
      
      break;
    }
    case 1:
    {
      if(novidade_page)
      {
        lcd.setCursor(0,0);
        lcd.print("    AMBIENTE    ");
        novidade_page = 0;
      }
      if(novidade_t)
      {
        lcd.setCursor(2,1);
        lcd.print(t);
        lcd.setCursor(6,1);
        lcd.write(7);
        lcd.print("C");
        novidade_t = 0;
      }
      if(novidade_h)
      {
        lcd.setCursor(11,1);
        lcd.print((int)h);
        lcd.print("%");
        novidade_h = 0;
      }
      
      break;
    }
    case 2:
    {
      if(novidade_set)
      {
        lcd.setCursor(0,1);
        lcd.print("      ");
        lcd.setCursor(0,1);
        
        lcd.print("MANUAL");
        novidade_set = 0;
      }
      if(novidade_temp)
      {
        numerao(disp_media *10);
        lcd.setCursor(12,1);
        lcd.print(",");
        novidade_temp = 0;
      }
      
      if(novidade_pwm)
      {
        lcd.setCursor(0,0);
        lcd.print("     ");
        lcd.setCursor(0,0);
        if(pid_contr > 0) lcd.print("+");
        lcd.print(map(pid_contr, -255, 255, -100, 100));
        lcd.print("%");
        novidade_pwm = 0;
      }
      
      break;
    }
    case 3:
    {
      ok = 1;
      novidade_page = 1;
      while(ok)
      {
        if(!retencao && !btn)
        {
          if(config_page != 5)
          {  
            config_page++;
            lcd.clear();
            novidade_page = 1;
            novidade_set = 1;
            retencao = 1;
            config_disp(config_page);
          }
          else
          {
            tempo_btn = millis();
            while(!btn);
            if((millis() - tempo_btn) < 1000)
            {
               config_page++;
               lcd.clear();
               novidade_set  = 1;
               novidade_page = 1;
               retencao = 1;
               return;
            }
            else
            {
              display_atual = 0;
              EEPROM.write(0, 0);
              EEPROM.write(1, contr);
              EEPROM.write(2, (unsigned short) kP);
              EEPROM.write(3, (unsigned short)(kI*50));
              EEPROM.write(4, (unsigned short)(kD*50));
              lcd.clear();
              return;
            }
          }
        }
        
  
        if((abs(antknob - knob)) > 5)
        {
          antknob = knob;
          novidade_set = 1;
        }
        if(config_page > 5) config_page = 0;
        if(btn) retencao = 0;
        if(novidade_set){
          config_disp(config_page);
          novidade_set  = 0;
          novidade_page = 0;
        }
      }
      delay(300);
      break;
    }
  }
}

void config_disp(unsigned short page)
{
  unsigned short  i;
           double j;
  switch(page)
  {
    case 0:
    {
      if(novidade_page)
      {
        lcd.setCursor(0,0);
        lcd.print("CONTRASTE: ");
        lcd.print(map(scontr, 0, 255, 0, 100));
        lcd.print("%");
        lcd.setCursor(8,1);
        lcd.print("-> ");
      }
      i = map(knob, 0, 1023, 0, 255);//15%
      if(novidade_set)
      {
        analogWrite(_contr, i);
        contr = i;
        lcd.setCursor(11,1);
        lcd.print("    ");
        lcd.setCursor(11,1);
        lcd.print(map(i, 0, 255, 0, 100));
        lcd.print("%");
      }
    }
    break;
    case 1:
    {
      if(novidade_page)
      {
        lcd.setCursor(0,0);
        lcd.print("PID    kP:      ");
        lcd.setCursor(11,0);
        lcd.print((unsigned short)skP);
        lcd.setCursor(8,1);
        lcd.print("-> ");
      }
      j = map(knob, 0, 1023, 0, 250);//150
      if(novidade_set)
      {
        kP = j;
        lcd.setCursor(11,1);
        lcd.print("     ");
        lcd.setCursor(11,1);
        lcd.print((int)j);
      }
    }
    break;
    case 2:
    {
      if(novidade_page)
      {
        lcd.setCursor(0,0);
        lcd.print("PID    kI:      ");
        lcd.setCursor(11,0);
        lcd.print(skI);
        lcd.setCursor(8,1);
        lcd.print("-> ");
      }
      j = map(knob, 0, 1023, 0, 250);//15%
      if(novidade_set)
      {
        kI = (float)(j/50);
        lcd.setCursor(11,1);
        lcd.print("    ");
        lcd.setCursor(11,1);
        lcd.print(j/50);
      }
    }
    break;
    case 3:
    {
      if(novidade_page)
      {
        lcd.setCursor(0,0);
        lcd.print("PID    kD:      ");
        lcd.setCursor(11,0);
        lcd.print(skD);
        lcd.setCursor(8,1);
        lcd.print("-> ");
      }
      j = map(knob, 0, 1023, 0, 250);//15%
      if(novidade_set)
      {
        kD = (float)(j/50);
        lcd.setCursor(11,1);
        lcd.print("    ");
        lcd.setCursor(11,1);
        lcd.print(j/50);
      }
    }
    break;
    case 4:
    {
      if(novidade_page)
      {
        lcd.setCursor(0,0);
        lcd.print("SEGURE 1s: GRAVA");
        lcd.setCursor(0,1);
        lcd.print("   CLIQUE: TESTA");
      }
      break;
    }
  }
}

void lerdht() {
  // Leitura da umidade
  h = dht.readHumidity();
  // Leitura da temperatura (Celsius)
  t = dht.readTemperature();

  if (isnan(h) || isnan(t)) // Verifica se algum valor do sensor retornou nulo
  {
    dhtteste = 1;           // Indica erro na leitura do sensor
  }
  else{
    dhtteste = 0;           // Indica que a leitura correta do sensor
  }
}

void ler18b20(){
  
  int i;                    // Variável auxiliar local
  int j;
  ds.reset();               // Reinicia a leitura do sensor
  ds.select(addr);          // Seleciona o último sensor encontrado
  ds.write(0x44,1);         // Pede os dados do sensor selecionado


  if(!ds_novo)
  {
    if ( !ds.search(addr)) {  // Verifica se há algum sensor diferente
      Serial.println("No more addresses.");
      Serial.println();
      ds.reset_search();      // Reinicia a busca
      fim_scan = 1;
      //scan = 0;
      delay(250);             // Dá tempo para a nova busca
      
    }
    if(addr[0] == 0x28) {      // Verifica se o sensor é compatível
        type_s = 0; 
        ds_novo = 1;           
    } 
  }
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);

  for ( i = 0; i < 9; i++) {
    data[i] = ds.read();
  }


  // convert the data to actual temperature

  unsigned int raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // count remain gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    if (cfg == 0x00) raw = raw << 3;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw << 2; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw << 1; // 11 bit res, 375 ms
    // default is 12 bit resolution, 750 ms conversion time
  }    
  
    if (raw & 0x8000){   //is minus ?
    celsius = 0 - ((float) ((raw ^ 0xffff) + 1) / 16.0); // 2's comp
  }
  else{
   celsius = (float)raw / 16.0;
  }

  for(i = 0;i < num_sensor; i++){
    for(j = 0; j < 8; j++){
      if(addr[j] == s_sensor[j][i]){
       if(j == 7){
        sensort[i+1] = celsius;
        err[i+1] = 0;
       }
      }
      else{
        err[i+1]++;
        j = 8;
      }
    }
    if(sensort[i+1] == 85){
      err[i+1]++;
    }
    
  }  
}

void numerao(int temp)
{
  unsigned int mod_temp = abs(temp);
  
  lcd.setCursor(8,0);
  lcd.print(" ");
  lcd.setCursor(5,0);
  lcd.print(" ");
  
  
  if(temp < 0)
  {
    if(mod_temp < 100)
    {
      lcd.setCursor(7,0);
    }
    else
    {
      lcd.setCursor(5,0);
    }
      

    switch(pisca_numerao)
    {
      case 0:
      {
        lcd.write(4);
        break;
      }
      case 1:
      {
        lcd.setCursor(5,0);
        lcd.print(" ");
        break;
      }
      case 2:
      {
        lcd.setCursor(7,0);
        lcd.print(" ");
        break;
      }
      case 3:
      {
        lcd.setCursor(7,0);
        lcd.print(" ");
        break;
      }
      break;
    }
  }
  else
  {
    lcd.setCursor(7,0);
    lcd.print(" ");
    lcd.setCursor(5,0);
    lcd.print(" ");
  }

  
  int temp3 = mod_temp  %10;
  int temp2 = (mod_temp/10) %10;
  int temp1 = mod_temp /100;
  
  x = 6;
  numero = temp1;
  if ((tempant1 != temp1)||(force_numerao == 1))
  {
    lcd.setCursor(6,0);
    lcd.print("   ");
    lcd.setCursor(6,1);
    lcd.print("   ");
    tempant1 = temp1;
    force_numerao = 0;
  }

  if(pisca_numerao != 1)
  {
    if((mod_temp > 99)||set_dezena) mostranumero();
  }
  
  x = 9;
  numero = temp2;
  if ((tempant2 != temp2)||(force_numerao == 2))
  {
    lcd.setCursor(9,0);
    lcd.print("   ");
    lcd.setCursor(9,1);
    lcd.print("   ");
    tempant2 = temp2;
    force_numerao = 0;
  }
  if(pisca_numerao != 2)
  {
    mostranumero();
  }
  x = 13;
  numero = temp3;
  if ((tempant3 != temp3)||(force_numerao == 3))
  {
    lcd.setCursor(13,0);
    lcd.print("   ");
    lcd.setCursor(13,1);
    lcd.print("   ");
    tempant3 = temp3;
    force_numerao = 0;
  }
  if(pisca_numerao != 3)
  {
    mostranumero();
  }
}

void mostranumero() //Mostra o numero na posicao definida por "X"
{
  switch(numero)
    {
      case 0:
      {
        lcd.setCursor(x, 0); //Seleciona a linha superior
        lcd.write((byte)0);  //Segmento 0 selecionado
        lcd.write(1);  //Segmento 1 selecionado
        lcd.write(2);
        lcd.setCursor(x, 1); //Seleciona a linha inferior
        lcd.write(3);  
        lcd.write(4);  
        lcd.write(5);
      }
      break;
      case 1:
      {
        lcd.setCursor(x+1,0);
        lcd.write(2);
        lcd.setCursor(x+1,1);
        lcd.write(5);
      }
      break;
      case 2:
      {
        lcd.setCursor(x,0);
        lcd.write(6);
        lcd.write(6);
        lcd.write(2);
        lcd.setCursor(x, 1);
        lcd.write(3);
        lcd.write(4);
        lcd.write(4);
      }
      break;
      case 3:
      {
        lcd.setCursor(x,0);
        lcd.write(6);
        lcd.write(6);
        lcd.write(2);
        lcd.setCursor(x, 1);
        lcd.write(4);
        lcd.write(4);
        lcd.write(5); 
      }
      break;
      case 4:
      {
        lcd.setCursor(x,0);
        lcd.write(3);
        lcd.write(4);
        lcd.write(2);
        lcd.setCursor(x+2, 1);
        lcd.write(5);
      }
      break;
      case 5:
      {
        lcd.setCursor(x,0);
        lcd.write((byte)0);
        lcd.write(6);
        lcd.write(6);
        lcd.setCursor(x, 1);
        lcd.write(4);
        lcd.write(4);
        lcd.write(5);
      }
      break;
      case 6:
      {
        lcd.setCursor(x,0);
        lcd.write((byte)0);
        lcd.write(6);
        lcd.write(6);
        lcd.setCursor(x, 1);
        lcd.write(3);
        lcd.write(4);
        lcd.write(5);
      }
      break;
      case 7:
      {
        lcd.setCursor(x,0);
        lcd.write(1);
        lcd.write(1);
        lcd.write(2);
        lcd.setCursor(x+1, 1);
        lcd.write((byte)0);
      }
      break;
      case 8:
      {
        lcd.setCursor(x,0);
        lcd.write((byte)0);
        lcd.write((byte)6);
        lcd.write(2);
        lcd.setCursor(x, 1);
        lcd.write(3);
        lcd.write(4);
        lcd.write(5);
      }
      break;
      case 9:
      {
        lcd.setCursor(x,0);
        lcd.write((byte)0);
        lcd.write((byte)6);
        lcd.write((byte)2);
        lcd.setCursor(x+2, 1);
        lcd.write((byte)5);
      }
      break;
    }
}

void controle(int set_pwm)
{
  unsigned short vento = 0;
  float aux;
  float auxErro;
  float auxControle;
  
  auxErro = abs( ((float)set/10) - celsius );
  
  if((float)set/10 > 10.0)
  {
    auxControle = 1.0;
  }
  else
  {
    aux = set;
    auxControle = (float)map(aux, -110, 100, 5, 100);
    auxControle /= 100.0;
  }
  
  if((auxErro > auxControle) && (display_atual < 2))
  {
    if(((float)set/10) > celsius)
    {
      P = 255;
      I = 0;
      D = 0;
      pid_contr = 255;
      analogWrite(_pwm, 255);
      digitalWrite(_esfria, LOW);
      digitalWrite(_aquece,HIGH); 
      novidade_pwm  = 1;
    }
    if(((float)set/10) < celsius)
    {
      P = -255;
      I = 0;
      D = 0;
      pid_contr = -255;
      analogWrite(_pwm, 255);
      digitalWrite(_esfria,HIGH);
      digitalWrite(_aquece, LOW);  
      novidade_pwm  = 1;

    }
    analogWrite(_vento, 255);
  }
  else
  {
    if(set_pwm == 0)
    {
      analogWrite(_pwm, 0);
      digitalWrite(_esfria,LOW);
      digitalWrite(_aquece,LOW);
    }
    if(set_pwm > 0){
      analogWrite(_pwm, abs(set_pwm));
      digitalWrite(_esfria, LOW);
      digitalWrite(_aquece,HIGH);
    }
    if(set_pwm < 0)
    {
      analogWrite(_pwm, abs(set_pwm));
      digitalWrite(_esfria,HIGH);
      digitalWrite(_aquece, LOW);  
    }
    vento = 80 + set_pwm;
    if(vento > 255) vento = 255;
    analogWrite(_vento, vento);
  }
  
}

void temp_set_passo()
{
  
  int i = 0;
  int ant_i = 10;
  int dezena = 0;
  int unidade = 0;
  int decimo = 0;

  float pisca_tempo = millis();
  float estavel_tempo = millis();
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("     ");
  lcd.setCursor(0,0);
  lcd.print("SETAR");
  lcd.setCursor(0,1);
  lcd.print("TEMP.");
  lcd.setCursor(12,1);
  lcd.print(",");

  P = 0;
  I = 0;
  D = 0;
  display_atual = 0;
  retencao = 1;
  novidade_set  = 1;
  novidade_page = 1;
  novidade_pwm  = 1;

  knob_set = 0;
  
  while(btn)
  {
    i = map(knob, 0, 1023, -9, 9);
    knob_pos = i;
    knob_antpos = i;
    if(i >  6) i =  6;
    if(i < -1) i = -1;
    if((ant_i != i)|| force_numerao)
    {
      set_dezena = 1;
      dezena = i;
      
      knob_set = abs(dezena)*100;
      if((dezena < 0)) knob_set *= -1;
      numerao(knob_set);
      set = knob_set;
  
    }

    if(ant_i != i)
    { 
      estavel_tempo = millis();
      pisca_numerao = 0;
    }
    ant_i = i;

    if((millis() - pisca_tempo) > 499) 
    {
      if((millis() - estavel_tempo) > 499)
      {
        if(pisca_numerao == 0)
        {
          pisca_numerao = 1;
        }
        else
        {
          pisca_numerao = 0;
        }
        force_numerao = 1;
        pisca_tempo = millis();
        estavel_tempo = millis();
      }
      else
      {
        pisca_numerao = 0;
      }
    }
  }
  
  set_dezena = 0;
  force_numerao = 1;
  pisca_numerao = 0;
  
  i = 0;
  ant_i = 10;
  while(!btn);

  delay(300);
  while(btn)
  {
    if(dezena == -1) return;
    if(dezena ==  6) return;
    i = map(knob, 0, 1023, -9, 9);
    knob_pos = i;
    knob_antpos = i;
    if(dezena != 0)
      if(i < 0) i = 0;
    

    if((ant_i != i)|| force_numerao)
    {
      unidade = i;
      knob_set = (abs(dezena)*100)+(abs(unidade)*10);
      if((dezena < 0) || (unidade < 0)) knob_set *= -1;
      numerao(knob_set);
      set = knob_set;
    }

    if(ant_i != i)
    { 
      estavel_tempo = millis();
      pisca_numerao = 0;
    }
    ant_i = i;

    if((millis() - pisca_tempo) > 499) 
    {
      if((millis() - estavel_tempo) > 499)
      {
        if(pisca_numerao == 0)
        {
          pisca_numerao = 2;
        }
        else
        {
          pisca_numerao = 0;
        }
        force_numerao = 2;
        pisca_tempo = millis();
        estavel_tempo = millis();
      }
      else
      {
        pisca_numerao = 0;
      }
    }
  }

  pisca_numerao = 0;
  
  i = 0;
  ant_i = 10;
  while(!btn);

  delay(300);
  while(btn)
  {
    i = map(knob, 0, 1023, -9, 9);
    knob_pos = i;
    knob_antpos = i;
    if((dezena != 0) || (unidade != 0))
      if(i < 0) i = 0;
    

    if((ant_i != i)|| force_numerao)
    {
      decimo = i;
      knob_set = (abs(dezena)*100)+(abs(unidade)*10)+abs(decimo);
      if((dezena < 0) || (unidade < 0) || (decimo < 0)) knob_set *= -1;
      numerao(knob_set);
      set = knob_set;
    }

    if(ant_i != i)
    { 
      estavel_tempo = millis();
      pisca_numerao = 0;
    }
    ant_i = i;

    if((millis() - pisca_tempo) > 499) 
    {
      if((millis() - estavel_tempo) > 499)
      {
        if(pisca_numerao == 0)
        {
          pisca_numerao = 3;
        }
        else
        {
          pisca_numerao = 0;
        }
        force_numerao = 3                 ;
        pisca_tempo = millis();
        estavel_tempo = millis();
      }
      else
      {
        pisca_numerao = 0;
      }
    }
  }
  pisca_numerao = 0;
}

void grava_set()
{
  EEPROM.write(6, (set & 0xFF));
  EEPROM.write(7, (set >> 8));
}

float media_movel()
{
  int i;
  float media = 0;
  for(i = 0; i < 9; i++)
  {
    media_temp[i] = media_temp[i+1];
  }
  media_temp[9] = celsius;
  for(i = 0; i < 10; i++)
  {
    media = media + media_temp[i];
  }
  media = media / 10;
  return media;
}

