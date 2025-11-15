#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define INITIAL_STACK_SIZE 1024

typedef struct {
  double *data;
  int size;
  int capacity;
} Stack;

// Funciones auxiliares para la pila
void stack_init(Stack *stack) {
  stack->data = malloc(INITIAL_STACK_SIZE * sizeof(double));
  stack->size = 0;
  stack->capacity = INITIAL_STACK_SIZE;
}

void stack_resize(Stack *stack, int new_capacity) {
  stack->data = realloc(stack->data, new_capacity * sizeof(double));
  stack->capacity = new_capacity;
}

static inline void stack_push(Stack *stack, double value) {
  if (stack->size == stack->capacity) {
    stack_resize(stack, stack->capacity + 256); // Incremento fijo
  }
  stack->data[stack->size++] = value;
}

static inline double stack_pop(Stack *stack) {
  if (stack->size == 0) {
    fprintf(stderr, "Pila vacía\n");
    exit(1);
  }
  return stack->data[--stack->size];
}

const double *stack_peek(const Stack *stack, int index) {
  if (index < 0 || index >= stack->size) {
    fprintf(stderr, "Índice fuera de rango en la pila\n");
    exit(1);
  }
  return &stack->data[index]; // Devuelve un puntero
}

void stack_free(Stack *stack) {
  free(stack->data);
  stack->data = NULL;
  stack->size = stack->capacity = 0;
}

void stack_print(const Stack *stack) {
    if (stack->size == 0) {
        printf("Stack vacío\n");
        return;
    }
    printf("Contenido del stack (de tope a base):\n");
    for (int i = stack->size - 1; i >= 0; i--) {
        printf("%f ", stack->data[i]);
    }
    printf("\n");
}

/**
 * @brief Evalúa una expresión en notación polaca inversa (RPN).
 *
 * @param expr Expresión RPN a evaluar.
 * @param t Valor de la variable 't' en la expresión.
 * @param multivalue_stack Indica si se permite un stack con múltiples valores al final.
 * @param debug_mode Indica si se permiten salidas de control para el desarrollo.
 * @return El resultado de la evaluación.
 */
double eval_rpn_f(const char *expr, double t, int multivalue_stack, int debug_mode) {
  Stack stack;
  stack_init(&stack);

  const char *ptr = expr;
  while (*ptr != '\0') {
    // Ignorar espacios
    if (isspace(*ptr)) {
      ptr++;
      continue;
    }

    // Procesar números
    if (isdigit(*ptr) || (*ptr == '-' && isdigit(*(ptr + 1)))) {
      int value = atof(ptr);
      stack_push(&stack, value);
      // Avanzar al siguiente token
      while (*ptr != '\0' && !isspace(*ptr)) ptr++;
    }
    // Procesar operadores
    else {
      char token[64];
      int i = 0;
      while (*ptr != '\0' && !isspace(*ptr)) {
        token[i++] = *ptr++;
      }
      token[i] = '\0';

      if (strcmp(token, "t") == 0) {
        // Variable 't'
        stack_push(&stack, t);

      } else if (strcmp(token, "swap") == 0) {
        // Operador swap
        if (stack.size < 2) {
          fprintf(stderr, "Falta de operandos para 'swap'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double b = stack_pop(&stack);
        stack_push(&stack, a);
        stack_push(&stack, b);

      } else if (strcmp(token, "dup") == 0) {
        // Operador dup
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'dup'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = *stack_peek(&stack, stack.size - 1);
        stack_push(&stack, a);

      } else if (strcmp(token, "put") == 0) {
        // Operador put
        if (stack.size < 2) {
          fprintf(stderr, "Falta de operandos para 'put'\n");
          stack_free(&stack);
          return NAN;
        }
        int index = (int)stack_pop(&stack);
        double value = stack_pop(&stack);
        if (index < 0 || index >= stack.size) {
          fprintf(stderr, "Índice inválido para 'put'\n");
          stack_free(&stack);
          return NAN;
        }
        int index_lifo = stack.size - 1 - index;
        stack.data[index_lifo] = value;

      } else if (strcmp(token, "drop") == 0) {
        // Operador drop
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'drop'\n");
          stack_free(&stack);
          return NAN;
        }
        stack_pop(&stack);

      } else if (strcmp(token, "pick") == 0) {
        // Operador pick
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'pick'\n");
          stack_free(&stack);
          return NAN;
        }
        int index = (int)stack_pop(&stack);
        if (index < 0 || index >= stack.size) {
          fprintf(stderr, "Índice inválido para 'pick'\n");
          stack_free(&stack);
          return NAN;
        }
        int index_lifo = stack.size - 1 - index;
        double value = *stack_peek(&stack, index_lifo);
        stack_push(&stack, value);

      } else if (strcmp(token, "only") == 0) {
        // Operador only
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'only'\n");
          stack_free(&stack);
          return 0;
        }
        int index = (int)stack_pop(&stack);
        if (index < 0 || index >= stack.size) {
          fprintf(stderr, "Índice inválido para 'only'\n");
          stack_free(&stack);
          return 0;
        }
        // Ajuste del índice para que sea relativo al tope de la pila
        int index_lifo = stack.size - 1 - index;
        double value_to_keep = *stack_peek(&stack, index_lifo);

        // Vaciar el stack
        stack_free(&stack);
        stack_init(&stack);

        // Empujar el valor correspondiente al índice ajustado
        stack_push(&stack, value_to_keep);

      } else if (strcmp(token, "~") == 0) {
        // Operador unario: Negación binaria (~)
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para '%s'\n", token);
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack); // Extraemos el operando superior
        int int_a = (int)a;           // Convertimos el operando a entero
        double result = ~int_a;       // Aplicamos la negación binaria
        stack_push(&stack, result);   // Devolvemos el resultado a la pila

      } else if (strcmp(token, "sin") == 0) {
        // Función seno
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'sin'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double result = sin(a); // Aplica la función seno
        stack_push(&stack, result);

      } else if (strcmp(token, "cos") == 0) {
        // Función coseno
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'cos'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double result = cos(a); // Aplica la función coseno
        stack_push(&stack, result);

      } else if (strcmp(token, "tan") == 0) {
        // Función tangente
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'tan'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double result = tan(a); // Aplica la función tangente
        stack_push(&stack, result);

      } else if (strcmp(token, "sqrt") == 0) {
        // Función raíz cuadrada
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'sqrt'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        if (a < 0) {
          fprintf(stderr, "Raíz cuadrada de número negativo\n");
          stack_free(&stack);
          return NAN;
        }
        double result = sqrt(a); // Aplica la función raíz cuadrada
        stack_push(&stack, result);

      } else if (strcmp(token, "log") == 0) {
        // Función logaritmo natural
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'log'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        if (a <= 0) {
          fprintf(stderr, "Logaritmo de número no positivo\n");
          stack_free(&stack);
          return NAN;
        }
        double result = log(a); // Aplica la función logaritmo natural
        stack_push(&stack, result);

      } else if (strcmp(token, "exp") == 0) {
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'exp'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double result = exp(a); // Aplica la función exponencial
        stack_push(&stack, result);

      } else if (strcmp(token, "abs") == 0) {
        // Función valor absoluto
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'abs'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double result = fabs(a); // Aplica la función valor absoluto
        stack_push(&stack, result);

      } else if (strcmp(token, "ceil") == 0) {
        // Función techo (redondeo hacia arriba)
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'ceil'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double result = ceil(a); // Aplica la función techo
        stack_push(&stack, result);

      } else if (strcmp(token, "floor") == 0) {
        // Función piso (redondeo hacia abajo)
        if (stack.size < 1) {
          fprintf(stderr, "Falta de operandos para 'floor'\n");
          stack_free(&stack);
          return NAN;
        }
        double a = stack_pop(&stack);
        double result = floor(a); // Aplica la función piso
        stack_push(&stack, result);

      } else if (strcmp(token, "pow") == 0) {
        // Función potencia (operador binario)
        if (stack.size < 2) {
          fprintf(stderr, "Falta de operandos para 'pow'\n");
          stack_free(&stack);
          return NAN;
        }
        double b = stack_pop(&stack); // Exponente
        double a = stack_pop(&stack); // Base
        double result = pow(a, b); // Calcula a^b
        stack_push(&stack, result);

      } else if (strcmp(token, "pi") == 0) {
        // Constante pi
        stack_push(&stack, M_PI); // Agrega el valor de pi a la pila

      } else if (strcmp(token, "e") == 0) {
        // Constante e (número de Euler)
        stack_push(&stack, M_E); // Agrega el valor de e a la pila

      } else if (strcmp(token, "inf") == 0) {
        // Infinito positivo
        stack_push(&stack, INFINITY); // Agrega infinito positivo a la pila

      } else if (strcmp(token, "nan") == 0) {
        // Not-a-Number
        stack_push(&stack, NAN); // Agrega NaN a la pila

      } else {
        // Operadores binarios
        if (stack.size < 2) {
          fprintf(stderr, "Falta de operandos para '%s'\n", token);
          stack_free(&stack);
          return NAN;
        }
        double b = stack_pop(&stack);
        double a = stack_pop(&stack);
        double result = NAN;

        if (strcmp(token, "+") == 0) {
          result = a + b;

        } else if (strcmp(token, "-") == 0) {
          result = a - b;

        } else if (strcmp(token, "*") == 0) {
          result = a * b;

        } else if (strcmp(token, "/") == 0) {
          if (b == 0) {
            fprintf(stderr, "División por cero\n");
            stack_free(&stack);
            return NAN;
          }
          result = a / b;

        } else if (strcmp(token, "%") == 0) {
          if (b == 0) {
            fprintf(stderr, "División por cero en módulo\n");
            stack_free(&stack);
            return NAN;
          }
          result = fmod(a, b); // Módulo para números flotantes

        } else if (strcmp(token, "==") == 0) {
          result = (a == b) ? 1 : 0;

        } else if (strcmp(token, "!=") == 0) {
          result = (a != b) ? 1 : 0;

        } else if (strcmp(token, ">") == 0) {
          result = (a > b) ? 1 : 0;

        } else if (strcmp(token, "<") == 0) {
          result = (a < b) ? 1 : 0;

        } else if (strcmp(token, ">=") == 0) {
          result = (a >= b) ? 1 : 0;

        } else if (strcmp(token, "<=") == 0) {
          result = (a <= b) ? 1 : 0;

        } else if (strcmp(token, "&&") == 0) {
          result = (a != 0 && b != 0) ? 1 : 0;

        } else if (strcmp(token, "||") == 0) {
          result = (a != 0 || b != 0) ? 1 : 0;

        } else if (strcmp(token, "<<") == 0) {
          result = (int)a << (int)b; // Desplazamiento a la izquierda

        } else if (strcmp(token, ">>") == 0) {
          result = (int)a >> (int)b; // Desplazamiento a la derecha

        } else if (strcmp(token, "&") == 0) {
          result = (int)a & (int)b; // AND bit a bit

        } else if (strcmp(token, "|") == 0) {
          result = (int)a | (int)b; // OR bit a bit

        } else if (strcmp(token, "^") == 0) {
          result = (int)a ^ (int)b; // XOR bit a bit

        } else {
          fprintf(stderr, "Operador desconocido '%s'\n", token);
          stack_free(&stack);
          return NAN;
        }
        stack_push(&stack, result);
      }
    }
  }

  if (multivalue_stack == 0) {
    if (stack.size != 1) {
      fprintf(stderr, "Expresión incompleta o incorrecta\n");
      stack_free(&stack);
      return NAN;
    }
  }

  if (debug_mode != 0){
    stack_print(&stack);
  }

  double result = stack_pop(&stack);
  stack_free(&stack);
  return result;
}
