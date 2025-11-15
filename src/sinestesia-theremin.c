// SINESTESIA-THEREMIN
// AUTORIA : Caalma + Lenguajín + Grok (optimización anti-clicks v2)
// 03-2025 → 11-2025

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <portaudio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <pthread.h>
#include <yaml.h>
#include "rpnf.h"

// Constantes de audio
#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define STREAM_BUFFER_SIZE 1000

// Colores de texto
#define TC_VERDE "\033[38;5;118m"
#define TC_ROJO "\033[38;5;196m"
#define TC_AZUL "\033[38;5;39m"
#define TC_GRIS "\033[38;5;241m"
#define TC_RESET "\033[0m"
#define TC_ROSA "\033[38;5;210m"

// --- VARIABLES GLOBALES ---
volatile sig_atomic_t tecla_pulsada = 0;
volatile sig_atomic_t ejecutando = 1;
volatile unsigned char ultima_tecla = 0;
volatile unsigned char penultima_tecla = 0;
volatile int bit_actual = 0;
volatile int bit_previo = 0;
static double fase = 0.0;
static const float tiempo_ataque = 0.01f;
static const float tiempo_liberacion = 0.08f;
static const float alpha_ataque = 0.8f / (tiempo_ataque * SAMPLE_RATE);
static const float alpha_liberacion = 0.2f / (tiempo_liberacion * SAMPLE_RATE);
static const float alpha = 0.9999f;
static double tiempo = 0.0;
static int instante = 0;
static float prev_muestra = 0.0f;
static float amplitud_actual = 0.0f;

// Efectos
static int conmutador_fx_ruido = 0;
static int conmutador_fx_eco = 0;
static int conmutador_fx_phaser = 0;
static int conmutador_fx_bitcrusher = 0;
static int conmutador_fx_whawha = 0;
static int conmutador_fx_reverberacion_g = 0;
static int conmutador_fx_anillo_ruido = 0;

// --- HILO RATÓN (con mutex + condición) ---
pthread_mutex_t mouse_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t mouse_cond = PTHREAD_COND_INITIALIZER;
volatile int mouse_x = 0, mouse_y = 0;
volatile int mouse_updated = 0;
float pantalla_w = 1920.0f, pantalla_h = 1080.0f;
pthread_t hilo_mouse;

// --- FRECUENCIA SUAVIZADA Y CACHÉ ---
//static float current_freq = 440.0f;
static float smooth_freq = 440.0f;
static float target_freq = 440.0f;
static float last_mouse_y = -1.0f;
static unsigned char last_u = 0, last_p = 0;

// --- SUAVIZADO DE RATÓN Y EFECTOS ---
static float smooth_mouse_x = 0.5f;
static float smooth_mouse_y = 0.5f;
static const float MOUSE_SMOOTH = 0.08f;
static const float FREQ_SMOOTH = 0.05f;

// --- EFECTOS SUAVIZADOS ---
static float smooth_cutoff = 1000.0f;
static float smooth_lfo_phase = 0.0f;
static float smooth_bit_depth = 16.0f;

// --- SUAVIZADO PARA MOD BIT (anti-pops en onda) ---
static float smooth_bit_mod = 0.0f;
static const float BIT_SMOOTH = 0.05f;

// --- EXPRESIÓN GLOBAL ---
pthread_mutex_t expr_mutex = PTHREAD_MUTEX_INITIALIZER;
char current_expression[256] = "440";

// --- MODO FRECUENCIA ---
typedef struct {
  float min;
  float max;
  char *template;     // Plantilla RPN: "100 _u * 440 +"
  char current_expr[256];  // Con valores reemplazados
} FrequencyMode;

FrequencyMode *modes = NULL;
int mode_count = 0;

// --- YAML: CONTAR ITEMS ---
int count_items(yaml_parser_t *parser, FILE *fh, const char *key){
  enum { STATE_START, STATE_FOUND_KEY, STATE_IN_LIST } state = STATE_START;
  yaml_event_t event = {0};
  int count = 0;

  if (!yaml_parser_initialize(parser)) return 0;
  yaml_parser_set_input_file(parser, fh);

  int si = 1;
  while (si) {
    if (!yaml_parser_parse(parser, &event)) break;
    switch(event.type) {
    case YAML_SCALAR_EVENT: {
      char *value = (char*)event.data.scalar.value;
      if (state == STATE_START && strcmp(value, key) == 0) state = STATE_FOUND_KEY;
      else if (state == STATE_IN_LIST) count++;
      break;
    }
    case YAML_SEQUENCE_START_EVENT:
      if (state == STATE_FOUND_KEY) state = STATE_IN_LIST;
      break;
    case YAML_NO_EVENT: si = 0; break;
    default: break;
    }
    yaml_event_delete(&event);
  }

  fseek(fh, 0, SEEK_SET);
  yaml_parser_delete(parser);
  return count;
}

// --- YAML: CARGAR MODOS ---
void assing_modes(yaml_parser_t *parser, FILE *fh, const char *key, const int entry_count){
  enum { STATE_START, STATE_FOUND_KEY, STATE_IN_LIST } state = STATE_START;
  yaml_event_t event = {0};

  if (!yaml_parser_initialize(parser)) exit(EXIT_FAILURE);
  yaml_parser_set_input_file(parser, fh);

  modes = malloc(entry_count * sizeof(FrequencyMode));
  mode_count = entry_count;
  float step = 1.0f / entry_count;
  int current_entry = 0;

  int si = 1;
  while (si) {
    if (!yaml_parser_parse(parser, &event)) break;
    switch(event.type) {
    case YAML_SCALAR_EVENT: {
      char *value = (char*)event.data.scalar.value;
      if (state == STATE_START && strcmp(value, key) == 0) {
        state = STATE_FOUND_KEY;
      } else if (state == STATE_IN_LIST) {
        modes[current_entry].min = current_entry * step;
        modes[current_entry].max = (current_entry + 1) * step;
        modes[current_entry].template = strdup(value);
        modes[current_entry].current_expr[0] = '\0';
        current_entry++;
      }
      break;
    }
    case YAML_SEQUENCE_START_EVENT:
      if (state == STATE_FOUND_KEY) state = STATE_IN_LIST;
      break;
    case YAML_SEQUENCE_END_EVENT:
      if (state == STATE_IN_LIST) state = STATE_START;
      break;
    case YAML_NO_EVENT: si = 0; break;
    default: break;
    }
    yaml_event_delete(&event);
  }
  yaml_parser_delete(parser);
}

// --- CARGAR CONFIG ---
void load_config(const char *filename) {
  yaml_parser_t parser;
  FILE *fh = fopen(filename, "r");
  if (!fh) { perror("Error al abrir config"); exit(EXIT_FAILURE); }

  int entry_count = count_items(&parser, fh, "frecuencias");
  if (entry_count == 0) { fprintf(stderr, "No hay frecuencias\n"); exit(EXIT_FAILURE); }

  assing_modes(&parser, fh, "frecuencias", entry_count);
  fclose(fh);
}

// --- REEMPLAZAR TEXTO ---
void replace_text(char *expr, const char *key, int value) {
  char temp[32]; snprintf(temp, sizeof(temp), "%d", value);
  char *pos;
  while ((pos = strstr(expr, key)) != NULL) {
    int key_len = strlen(key), temp_len = strlen(temp);
    if (strlen(expr) + temp_len - key_len >= 256) break;
    memmove(pos + temp_len, pos + key_len, strlen(pos + key_len) + 1);
    memcpy(pos, temp, temp_len);
  }
}

// --- CALCULAR FRECUENCIA (optimizada) ---
void update_frequency(float selector, unsigned char u, unsigned char p) {
  for(int i = 0; i < mode_count; i++) {
    if(selector >= modes[i].min && selector < modes[i].max) {
      instante++;
      strcpy(modes[i].current_expr, modes[i].template);
      replace_text(modes[i].current_expr, "_u", u);
      replace_text(modes[i].current_expr, "_p", p);
      replace_text(modes[i].current_expr, "_t", instante);

      double result = eval_rpn_f(modes[i].current_expr, 0.0, 0, 0);
      if (result > 20 && result < 20000) target_freq = (float)result;

      pthread_mutex_lock(&expr_mutex);
      snprintf(current_expression, sizeof(current_expression), "[%s] → %.1f", modes[i].current_expr, result);
      pthread_mutex_unlock(&expr_mutex);
      return;
    }
  }
}

// --- HILO DEL RATÓN ---
void* mouse_data_update(void* arg) {
  Display *display = XOpenDisplay(NULL);
  if (!display) {
    print_error("ERROR: No hay X11 (ejecuta desde entorno gráfico)");
    exit(1);
  }
  Window root = DefaultRootWindow(display);

  while (ejecutando) {
    int x, y;
    Window child, root_ret;
    unsigned int mask;
    int win_x, win_y;
    XQueryPointer(display, root, &root_ret, &child, &x, &y, &win_x, &win_y, &mask);

    pthread_mutex_lock(&mouse_mutex);
    mouse_x = x; mouse_y = y;
    mouse_updated = 1;
    pthread_mutex_unlock(&mouse_mutex);
    pthread_cond_signal(&mouse_cond);

    usleep(5000); // 200 Hz máx
  }
  XCloseDisplay(display);
  return NULL;
}

// --- ACTUALIZAR PANTALLA ---
void update_screen_data() {
  Display *display = XOpenDisplay(NULL);
  if (display) {
    Screen* screen = DefaultScreenOfDisplay(display);
    pantalla_w = (float)screen->width;
    pantalla_h = (float)screen->height;
    XCloseDisplay(display);
  }
}

// --- TERMINAL ---
void terminal_clearline() { printf("\033[2K\r"); }
void terminal_clearscreen() { printf("\033[2J\033[H"); }
void terminal_config() {
  struct termios ttystate;
  tcgetattr(STDIN_FILENO, &ttystate);
  ttystate.c_lflag &= ~(ICANON | ECHO | ECHONL);
  ttystate.c_cc[VMIN] = 0;
  ttystate.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

void print_welcome() {
  terminal_clearscreen();
  printf("**%sSINESTESIA%sTHEREMIN%s**\n", TC_ROJO, TC_AZUL, TC_RESET);
  printf("Pulsa teclas → sonido. Ctrl+? para ayuda.\n");
}

void help_print(){
  terminal_clearline();
  printf(" --- Ctrl + e     Conmuta el FX Eco\n");
  printf(" --- Ctrl + r     Conmuta el FX Ruido\n");
  printf(" --- Ctrl + p     Conmuta el FX Phaser\n");
  printf(" --- Ctrl + b     Conmuta el FX BitCrusher\n");
  printf(" --- Ctrl + w     Conmuta el FX Wha-Wha\n");
  printf(" --- Ctrl + g     Conmuta el FX Reverberación Granular\n");
  printf(" --- Ctrl + a     Conmuta el FX Anillo de Ruido\n");
  printf(" --- Ctrl + ?     Muestra esta ayuda\n");
  printf(" --- Ctrl + c     Cierra el programa\n");
}


void print_error(char text[]) { fprintf(stderr, "%s%s%s\n", TC_ROSA, text, TC_RESET); }

void print_dynamic_bar(char title[], int value) {
  printf(" %s→", title);
  for(int i=0; i<value; i++) printf("█");
  printf(" ");
}

void status_bar_print() {
  terminal_clearline();
  printf(TC_GRIS);
  print_dynamic_bar("X", (int)(smooth_mouse_x * 20));
  print_dynamic_bar("Y", (int)(smooth_mouse_y * 20));

  int fx = conmutador_fx_ruido + conmutador_fx_eco + conmutador_fx_phaser +
           conmutador_fx_bitcrusher + conmutador_fx_whawha +
           conmutador_fx_reverberacion_g + conmutador_fx_anillo_ruido;
  if (fx > 0) {
    printf("  FX(");
    if(conmutador_fx_ruido) printf("RUIDO ");
    if(conmutador_fx_eco) printf("ECO ");
    if(conmutador_fx_phaser) printf("PHASER ");
    if(conmutador_fx_bitcrusher) printf("BCRUSH ");
    if(conmutador_fx_whawha) printf("WHA ");
    if(conmutador_fx_reverberacion_g) printf("REVG ");
    if(conmutador_fx_anillo_ruido) printf("RING ");
    printf(")");
  }

  pthread_mutex_lock(&expr_mutex);
  printf("  %s%.*s", TC_VERDE, 80, current_expression);
  pthread_mutex_unlock(&expr_mutex);
  printf("%s\r", TC_RESET);
  fflush(stdout);
}

// --- CALLBACK DE AUDIO ---
static int audio_callback(const void *input, void *output,
                          unsigned long frameCount,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData) {

  float *out = (float*)output;
  float target_amplitud = tecla_pulsada ? 0.5f : 0.0f;

  // --- ACTUALIZAR RATÓN (solo si cambió) ---
  pthread_mutex_lock(&mouse_mutex);
  if (mouse_updated) {
    float target_x = (float)mouse_x / pantalla_w;
    float target_y = (float)mouse_y / pantalla_h;
    smooth_mouse_x += (target_x - smooth_mouse_x) * MOUSE_SMOOTH;
    smooth_mouse_y += (target_y - smooth_mouse_y) * MOUSE_SMOOTH;
    mouse_updated = 0;
  }
  pthread_mutex_unlock(&mouse_mutex);

  float nmouse_x = smooth_mouse_x;
  float nmouse_y = smooth_mouse_y;

  // --- ACTUALIZAR FRECUENCIA (solo si cambia) ---
  if (fabs(nmouse_y - last_mouse_y) > 0.001f ||
      ultima_tecla != last_u || penultima_tecla != last_p) {
    update_frequency(nmouse_y, ultima_tecla, penultima_tecla);
    last_mouse_y = nmouse_y;
    last_u = ultima_tecla;
    last_p = penultima_tecla;
  }

  // --- RAMP DE FRECUENCIA ---
  smooth_freq += (target_freq - smooth_freq) * FREQ_SMOOTH;
  float frecuencia = smooth_freq;

  int trino = (int)(1024.0f * nmouse_y);

  for (unsigned long i = 0; i < frameCount; i++) {
    // Amplitud
    if (tecla_pulsada) {
      amplitud_actual += (target_amplitud - amplitud_actual) * alpha_ataque;
    } else {
      amplitud_actual += (target_amplitud - amplitud_actual) * alpha_liberacion;
    }

    // Bit suavizado
    int bit_raw = (ultima_tecla >> (7 - bit_actual)) & 1;
    float target_bit_mod = bit_raw * (trino / 1024.0f);
    smooth_bit_mod += (target_bit_mod - smooth_bit_mod) * BIT_SMOOTH;

    // Fase con mod suavizado
    float freq_mod = frecuencia * (1.0f + smooth_bit_mod * 0.5f);
    fase += freq_mod / SAMPLE_RATE;
    if (fase >= 1.0) fase -= 1.0;

    // Onda con offset suavizado
    float muestra = (sinf(2.0f * M_PI * fase) + (smooth_bit_mod * 0.3f)) * amplitud_actual;
    muestra = tanhf(muestra * 1.5f) / 1.5f;
    muestra = alpha * (prev_muestra + muestra - prev_muestra);
    prev_muestra = muestra;

    // Zona Glitch
    // --- EFECTOS (todos suavizados) ---
    // Efecto 1: Clipping (ruido)
    if (conmutador_fx_ruido) {
      if (rand() % 500 < 3) {
        float r = (rand() % 5 + 1);
        float noise = (nmouse_y < 0.5f ? tanf(muestra * r) : cosf(muestra * r)) * 0.1f; // Reducir amp
        muestra += noise;
      }
    }

    // Efecto 2: Eco (retroalimentación)
    if (conmutador_fx_eco) {
      static float buffer_eco[SAMPLE_RATE] = {0};
      static int idx_eco = 0;
      float delayed = buffer_eco[idx_eco];
      muestra += delayed * 0.3f; // Dry + wet
      buffer_eco[idx_eco] = muestra + delayed * 0.6f; // Feedback <1
      idx_eco = (idx_eco + 1) % SAMPLE_RATE;
    }

    // Efecto 3: Filtro Phaser con LFO Caótico
    if (conmutador_fx_phaser) {
      static float phaser_buffer[1024] = {0};
      static int phaser_idx = 0;
      float mod = nmouse_x * 0.05f;
      float lfo = sinf(smooth_lfo_phase) * mod;
      smooth_lfo_phase += 0.01f + nmouse_y * 0.05f;
      if (smooth_lfo_phase > 2*M_PI) smooth_lfo_phase -= 2*M_PI;

      float delayed = phaser_buffer[phaser_idx];
      float output = delayed + (muestra - delayed) * 0.7f;
      phaser_buffer[phaser_idx] = muestra + delayed * 0.7f;
      phaser_idx = (phaser_idx + 1) % 1024;
      muestra = output * 0.5f + muestra * 0.5f;
    }

    // Efecto 4: Distorsion BitCrusher
    // Degradación digital controlada
    if (conmutador_fx_bitcrusher) {
      float target_bd = 8.0f + 16.0f * nmouse_x;
      smooth_bit_depth += (target_bd - smooth_bit_depth) * 0.1f;
      float sr_reduction = 44100.0f / (100.0f + 43100.0f * nmouse_x);

      static float hold = 0, count = 0;
      count += 1.0f;
      if (count >= sr_reduction) {
        hold = floorf(muestra * powf(2, smooth_bit_depth)) / powf(2, smooth_bit_depth);
        count = 0;
      }
      muestra = hold * 0.8f + muestra * 0.2f;
    }

    // Efecto 5: wha-wha
    // Filtro de pico modulado
    if (conmutador_fx_whawha) {
      // --- LFO para modulación de cutoff ---
      static float lfo_phase = 0.0f;
      lfo_phase += 2.0f * M_PI * (0.5f + 2.0f * nmouse_x) / SAMPLE_RATE;  // 0.5 a 2.5 Hz
      if (lfo_phase >= 2.0f * M_PI) lfo_phase -= 2.0f * M_PI;
      float lfo = (sinf(lfo_phase) + 1.0f) * 0.5f;  // 0.0 a 1.0

      // --- Cutoff modulado ---
      float min_cutoff = 300.0f;
      float max_cutoff = 3000.0f;
      float target_cutoff = min_cutoff + (max_cutoff - min_cutoff) * lfo;

      static float smooth_cutoff = 1000.0f;
      smooth_cutoff += (target_cutoff - smooth_cutoff) * 0.2f;

      // --- Filtro Peaking (segundo orden) ---
      float fc = smooth_cutoff / SAMPLE_RATE;
      float Q = 1.0f + 10.0f * nmouse_y;  // Resonancia: 1 a 11

      float w0 = 2.0f * M_PI * fc;
      float alpha = sinf(w0) / (2.0f * Q);
      float a0 = 1.0f + alpha;
      float a1 = -2.0f * cosf(w0);
      float a2 = 1.0f - alpha;
      float b0 = alpha;           // Ganancia en el pico
      float b1 = 0.0f;
      float b2 = -alpha;

      // Normalizar
      float inv_a0 = 1.0f / a0;
      b0 *= inv_a0; b1 *= inv_a0; b2 *= inv_a0;
      a1 *= inv_a0; a2 *= inv_a0;

      // Estado del filtro
      static float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
      float x0 = muestra;

      float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

      // Actualizar estado
      x2 = x1; x1 = x0;
      y2 = y1; y1 = y0;

      // Mezclar: 70% filtrado + 30% seco
      muestra = y0 * 0.7f + muestra * 0.3f;
    }

    // Efecto 6: Reveberación Granular
    // Espacialización con granos de sonido
    if (conmutador_fx_reverberacion_g) {
      static float grain_buffer[SAMPLE_RATE] = {0};
      static int write_pos = 0;
      grain_buffer[write_pos] = muestra;
      write_pos = (write_pos + 1) % SAMPLE_RATE;

      float wet = 0;
      for(int g = 0; g < 8; g++) {
        int delay = 1000 + rand() % 10000;
        int pos = (write_pos - delay + SAMPLE_RATE) % SAMPLE_RATE;
        wet += grain_buffer[pos] * (1.0f - g/8.0f);
      }
      muestra = muestra * 0.7f + wet * 0.3f * nmouse_x;
    }

    // Efecto 7: Anillo de ruido
    // Texturas metálicas con componente aleatoria
    if (conmutador_fx_anillo_ruido) {
      float carrier = sinf(2*M_PI * (220.0f + 880.0f * nmouse_x) * tiempo);
      float mod = sinf(2*M_PI * (50.0f + 500.0f * nmouse_y) * tiempo) + ((rand()%2000-1000)/1000.0f)*0.3f;
      muestra += carrier * mod * 0.2f;
    }

    // Clip final anti-pops
    muestra = fmaxf(fminf(muestra, 1.0f), -1.0f);

    // --- SALIDA ESTÉREO ---
    if (nmouse_x <= 0.33f) {
      *out++ = muestra * nmouse_y;
      *out++ = muestra;
    } else if (nmouse_x <= 0.66f) {
      *out++ = muestra;
      *out++ = muestra;
    } else {
      *out++ = muestra;
      *out++ = muestra * nmouse_x;
    }

    // Avanzar bit cada 0.1s
    tiempo += 1.0 / SAMPLE_RATE;
    if (tiempo >= 0.1) {
      bit_actual = (rand() % 5 < 2) ? ((bit_actual & bit_previo) + 1) % 8 : (bit_actual + 1) % 8;
      tiempo = 0.0;
    }
  }
  return paContinue;
}

// --- MAIN ---
int main(int argc, char *argv[]) {

  if (argc < 2) { fprintf(stderr, "Uso: %s config.yml\n", argv[0]); return 1; }

  // --- SILENCIAR PORTAUDIO ---
  (void)freopen("/dev/null", "w", stderr);

  PaError err = Pa_Initialize();
  if (err != paNoError) {
    (void)freopen("/dev/tty", "w", stderr);
    fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
    return 1;
  }
  (void)freopen("/dev/tty", "w", stderr);


  load_config(argv[1]);
  update_screen_data();
  terminal_config();
  print_welcome();

  // Iniciar audio
  Pa_Initialize();
  PaStream *stream;
  PaStreamParameters outParams = {
    .device = Pa_GetDefaultOutputDevice(),
    .channelCount = 2,
    .sampleFormat = paFloat32,
    .suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice())->defaultLowOutputLatency,
    .hostApiSpecificStreamInfo = NULL
  };
  Pa_OpenStream(&stream, NULL, &outParams, SAMPLE_RATE, STREAM_BUFFER_SIZE, paClipOff, audio_callback, NULL);
  Pa_StartStream(stream);

  // Iniciar ratón
  pthread_create(&hilo_mouse, NULL, mouse_data_update, NULL);

  // Bucle principal
  while (ejecutando) {
    fd_set set; struct timeval tv = {0, 1000};
    FD_ZERO(&set); FD_SET(STDIN_FILENO, &set);

    if (select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) > 0) {
      char c = getchar();
      if (ultima_tecla != c) {
        penultima_tecla = ultima_tecla;
        bit_previo = bit_actual;
        ultima_tecla = c;
        bit_actual = 0;
      }

      // Controles
      if (c == 5) conmutador_fx_eco ^= 1;           // Ctrl+e
      if (c == 18) conmutador_fx_ruido ^= 1;        // Ctrl+r
      if (c == 16) conmutador_fx_phaser ^= 1;       // Ctrl+p
      if (c == 2) conmutador_fx_bitcrusher ^= 1;    // Ctrl+b
      if (c == 23) conmutador_fx_whawha ^= 1;       // Ctrl+w
      if (c == 7) conmutador_fx_reverberacion_g ^= 1; // Ctrl+g
      if (c == 1) conmutador_fx_anillo_ruido ^= 1;  // Ctrl+a
      if (c == 127) help_print();                   // Ctrl+?

      tecla_pulsada = 1;
      terminal_clearline();
      printf(" (%s%c%s) → ", TC_VERDE, c, TC_RESET);
      for (int i = 7; i >= 0; i--) printf("%s%d%s", (c >> i) & 1 ? TC_ROJO : TC_AZUL, (c >> i) & 1, TC_RESET);
      printf("\n");
    } else {
      tecla_pulsada = 0;
    }

    status_bar_print();
    usleep(10000);
  }

  // Limpieza
  ejecutando = 0;
  pthread_join(hilo_mouse, NULL);
  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();

  for(int i=0; i<mode_count; i++) free(modes[i].template);
  free(modes);
  return 0;
}
