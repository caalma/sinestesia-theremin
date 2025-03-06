// SINESTESIA-THEREMIN
// AUTORIA : Caalma + Lenguajín
// 03-2025

#include <stdio.h>
#include <stdlib.h>
#include <portaudio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <pthread.h>

// Constantes de audio
#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define STREAM_BUFFER_SIZE 1000

// Colores de texto para la terminal
#define TC_VERDE "\033[38;5;118m"
#define TC_ROJO "\033[38;5;196m"
#define TC_AZUL "\033[38;5;39m"
#define TC_GRIS "\033[38;5;241m"
#define TC_RESET "\033[0m"
#define TC_ROSA "\033[38;5;210m"

// Variables globales
volatile sig_atomic_t tecla_pulsada = 0;
volatile sig_atomic_t ejecutando = 1;
volatile unsigned char ultima_tecla = 0;
volatile unsigned char penultima_tecla = 0;
volatile int bit_actual = 0;
volatile int trino = 1;
volatile int bit_previo = 0;
volatile double fase = 0.0;
static const float tiempo_ataque = 0.01f; // 10ms para el fade-in
static const float tiempo_liberacion = 0.08f; // 80ms para el fade-out
static const float alpha_ataque = 0.8f / (tiempo_ataque * SAMPLE_RATE);
static const float alpha_liberacion = 0.2f / (tiempo_liberacion * SAMPLE_RATE);
static const float alpha = 0.9999f;
static double tiempo = 0.0;
static float prev_muestra = 0.0f;
static float amplitud_actual = 0.0f;

float pantalla_w = 0.0;
float pantalla_h = 0.0;
int mouse_x = 0, mouse_y = 0;
pthread_t hilo_mouse;



void terminal_clearline(){
  printf("\033[2K\r");
}


void terminal_clearscreen(){
  printf("\033[2J\033[H");
}


void terminal_config() {
  struct termios ttystate;
  tcgetattr(STDIN_FILENO, &ttystate);
  ttystate.c_lflag &= ~(ICANON | ECHO | ECHONL);
  ttystate.c_cc[VMIN] = 0;
  ttystate.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}


void print_welcome(){
  terminal_clearscreen();
  printf("**%sSINESTESIA%sTHEREMIN%s**\n",
         TC_ROJO, TC_AZUL, TC_RESET);
  printf("Pulsa teclas (el sonido es tu firma digital):\n");
  printf("Pulsa Ctrl+C para salir.\n");
}


void print_error(char text[]){
  fprintf(stderr, "%s%s%s\n", TC_ROSA, text, TC_RESET);
}


void update_screen_data() {
  Display *display = XOpenDisplay(NULL);
  Screen* screen = DefaultScreenOfDisplay(display);
  if (screen) {
    pantalla_w = (float)screen->width;
    pantalla_h = (float)screen->height;
  }
}

void* mouse_data_update() {
  Display *display = XOpenDisplay(NULL);
  if(!display) {
    print_error("ERROR: ¿Ejecutas desde terminal gráfico?\nPrueba desde una TTY con X (Ctrl+Alt+F2 no sirve)");
    exit(1);
  }
  int win_x, win_y;
  unsigned int mask;
  Window root = DefaultRootWindow(display);
  Window child;

  while(ejecutando) {
    XQueryPointer(display, DefaultRootWindow(display), &root, &child,
                  &mouse_x, &mouse_y, &win_x, &win_y, &mask);
    usleep(10000); // 10ms
  }
  XCloseDisplay(display);
  return NULL;
}


void print_dynamic_bar(char title[], int value){
  printf(" %s → ", title);
  for(int i=0; i < value; i++) printf("█");
  printf(" ");
}


void mouse_data_print() {
  terminal_clearline();
  printf(TC_GRIS);
  print_dynamic_bar("X", (int)((mouse_x / pantalla_w) * 20));
  print_dynamic_bar("Y", (int)((mouse_y / pantalla_h) * 20));
  printf("%s\r", TC_RESET);
}


static int audio_callback(const void *input, void *output,
                          unsigned long frameCount,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData) {

  // Supuestamente silencia salidas de logs de portaudio
  (void)input; (void)timeInfo; (void)statusFlags; (void)userData;

  float *out = (float*)output;
  float target_amplitud = tecla_pulsada ? 0.5f : 0.0f;
  float frecuencia = 0.0;
  float nmouse_x = mouse_x / pantalla_w;
  float nmouse_y = mouse_y / pantalla_h;

  float nm = (nmouse_x + nmouse_y) / 2;

  // Modos de frecuencias
  if(nm > 0 && nm <= 0.15){
    frecuencia = sin(ultima_tecla) * log(penultima_tecla + 1) * 55.0;

  }else if(nm > 0.15 && nm <= 0.30){
    frecuencia = ultima_tecla * penultima_tecla * 0.07;

  }else if(nm > 0.30 && nm <= 0.45){
    frecuencia = tanh(ultima_tecla) * log(penultima_tecla + 1) * 55.0;

  }else if(nm > 0.45 && nm <= 0.60){
    frecuencia = (ultima_tecla * 20.0) -  (penultima_tecla * 9.0);

  }else if(nm > 0.60 && nm <= 0.75){
    frecuencia = 110.0 + (ultima_tecla * 12.0) - (~penultima_tecla * 10.0);

  }else if(nm > 0.75){
    frecuencia = 210.0 + (ultima_tecla * 12.0);
  }

  trino = (int)1024.0 * nmouse_y;

  for (long unsigned int i = 0; i < frameCount; i++) {
    // Suavizado de amplitud
    if (tecla_pulsada) {
      amplitud_actual += (target_amplitud - amplitud_actual) * alpha_ataque;
    } else {
      amplitud_actual += (target_amplitud - amplitud_actual) * alpha_liberacion;
    }

    // Obtener bit actual del carácter (8 bits)
    int bit = (ultima_tecla >> (7 - bit_actual)) & trino;

    // Modulación caótica
    fase += (frecuencia * (1 + (bit * 0.5))) / SAMPLE_RATE;
    if (fase >= 1.0) fase -= 1.0;

    // Generar onda híbrida (cuadrada + senoidal)
    float muestra = (sin(2 * M_PI * fase) + (bit * 0.3)) * amplitud_actual;
    if (muestra > 1.0) muestra = 1.0;
    if (muestra < -1.0) muestra = -1.0;

    // Ajusar muestra
    muestra = tanh(muestra * 1.5) / 1.5; // Compresión no lineal
    muestra = alpha * (prev_muestra + muestra - prev_muestra); // Filtro HPF
    prev_muestra = muestra;

    // Zona Glitch
    float muestra_glitch = muestra;
    if (rand() % 500 < 3) {
      // Efecto 1: Clipping caótico
      if(nmouse_y > 0 && nmouse_y <= 0.50){
        muestra_glitch = tanf(muestra * (rand() % 5 + 1));
      }else if(nmouse_y > 0.50){
        muestra_glitch = cos(muestra * (rand() % 5 + 1));
      }

      // Efecto 2: Eco diabólico (retroalimentación)
      static float buffer_eco[SAMPLE_RATE] = {0};
      static int idx_eco = 0;
      muestra_glitch += buffer_eco[idx_eco] * 0.3;
      buffer_eco[idx_eco] = muestra_glitch;
      idx_eco = (idx_eco + 1) % SAMPLE_RATE;
    }
    muestra += muestra_glitch * 0.15;

    // Creando la salida
    if(nmouse_x > 0 && nmouse_x <= 0.33){
      *out++ = muestra * nmouse_y;
      *out++ = muestra;
    }else if(nmouse_x > 0.33 && nmouse_x <= 0.66){
      *out++ = muestra;
      *out++ = muestra;
    }else if(nmouse_x > 0.66){
      *out++ = muestra;
      *out++ = muestra * nmouse_x;
    }

    // Avanzar bit cada 0.1 segundos (ritmo binario)
    tiempo += 1.0 / SAMPLE_RATE;
    if (tiempo >= 0.1) {
      if(rand() % 5 < 2){
        bit_actual = ((bit_actual & bit_previo) + 1) % 8;
      }else{
        bit_actual = (bit_actual + 1) % 8;
      }
      tiempo = 0.0;
    }
  }
  return paContinue;
}


int main() {
  // Iniciar stream
  PaStream *stream;
  Pa_Initialize();
  PaStreamParameters outputParams;
  outputParams.device = Pa_GetDefaultOutputDevice();
  outputParams.channelCount = 2;
  outputParams.sampleFormat = paFloat32;
  outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
  outputParams.hostApiSpecificStreamInfo = NULL;
  Pa_OpenStream(&stream, NULL, &outputParams,
                SAMPLE_RATE, STREAM_BUFFER_SIZE,
                paClipOff, audio_callback, NULL);
  Pa_StartStream(stream);

  // Activar hilo del mouse
  pthread_create(&hilo_mouse, NULL, mouse_data_update, NULL);

  // Iniciar pantalla y terminal
  update_screen_data();
  terminal_config();
  print_welcome();

  // Ciclo principal
  while(1) {
    fd_set set;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000;

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    if (select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) > 0) {
      char c = getchar();
      if( ultima_tecla != c){
        penultima_tecla = ultima_tecla;
        bit_previo = bit_actual;
        ultima_tecla = c;
        bit_actual = 0;
      }
      tecla_pulsada = 1;
      terminal_clearline();
      printf(" (%s%c%s)  →  ", TC_VERDE, c, TC_RESET);
      for (int i = 7; i >= 0; i--){
        printf("%s%d%s", (c >> i) & 1 ? TC_ROJO : TC_AZUL, (c >> i) & 1, TC_RESET);
      }
      printf("\n");

    }else{
      tecla_pulsada = 0;
    }

    mouse_data_print();
    fflush(stdout);
    usleep(10000);
  }

  ejecutando = 0;
  pthread_join(hilo_mouse, NULL);
  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
  return 0;
}
