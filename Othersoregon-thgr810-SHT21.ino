/*
 * ============================================================
 *      Emulador Oregon V3 THGR810/THGR800/THGN800/THGN801 
 * ============================================================
 * Autor:   Víctor García Carbonell
 * Versión: 1.0
 * Fecha:   Octubre 2026
 *
 * Hardware:
 *      Micro:
 *           Con enable_opt: probado con Atmega328pb (Puede funcionar con otros Atmegas similares)
 *           Sin enable_opt: cualquier arduino.
 *      Emisor 433Mhz:
 *           FS1000A
 *          
 * Este proyecto incorpora código derivado de:
 *      Lars Wessels (2021), bajo licencia MIT.
 *      Copyright (c) 2021 Lars Wessels
 *      El código original ha sido modificado, mejorado, adaptado como código para este proyecto.
 * 
 * Licencia de este proyecto:
 *      Copyright (c) 2026 Víctor García Carbonell
 *      Licencia: MIT
 * ============================================================
 */

//Librería sensor
#include <SHT21.h>

//Modo del programa
    // Tipos de programa:
        // mod_program = 0 :
            // Emula el 100% el sensor original de Oregon
            // Habilita el modo de sync durante 30m (como los sensores reales), lo que permite a los receptores de Oregon originales conectarse a ese sensor realizando un "SEARCH", sin tener que realizarles un reseteo cuando el (rolling code) cambia. 
            // El rolling code es aleatorio (como los sensores reales).
            // Inconvenientes: si la batería, corriente del arduino se para, los receptores originales de Oregon perderán la señal ya que habrá cambiado el "rolling code" y se necesitará realizar un "Search" dentro de los primeros 30m desde el reinicio/encendido.
        // mod_program = 1 : (Recomendado)
            // Habilita el modo de sync durante 30m (como los sensores reales), lo que permite a los receptores de Oregon originales conectarse a ese sensor realizando un "SEARCH", sin tener que realizarles un reseteo cuando el (rolling code) cambia. 
            // El rolling code será "fijo" (Modificar el rcode). En caso de varios dispositivos con el mismo "Identificador del dispositivo (sensor_id)" y mismo canal, modificarlo para evitar colisiones.
            // Ventaja: en caso de que se apague el arduino o se resetee, los receptores Oregon originales volverán a detectar el sensor sin necesidad de darle al "Search" o realizar alguna acción sobre este.
        // mod_program = 2 :
            // Modo de sync siempre activo, por lo que cualquier receptor Original de Oregon ("con el SEARCH") podrá registrarlo en cualquier momento.
            // El rolling code será "fijo" (Modificar el rcode). En caso de varios dispositivos con el mismo "Identificador del dispositivo (sensor_id)" y mismo canal, modificarlo para evitar colisiones.
            // Inconveniente: Si tienes varios sensores del mismo tipo (sensor_id, Channel), cuando el receptor Original busque "con el SEARCH", puede que se sincronice con otro sensor 
        // mod_program = 3 :
            // Modo sync siempre desactivado
            // El rolling code será "fijo" y se establece en el apartado de "Variables de emulación del sensor"
            // Inconveniente: si por algún casual cambia el rolling code, necesitarás realizar un reset de el receptor Oregon original para que detecte el sensor, no servirá con un "Search".
        // mod_program = 4 (No recomendado):
            // Modo sync desactivado.
            // EL rolling code es aleatorio.
            // NO TIENE NINGUNA VENTAJA, TODO SON INCONVENIENTES
static int mod_program = 1;

//Optimización de consumo del arduino (Probado con atmega328pb "consumo en reposo de 0.003mA")
    //SOLO HABILITAR cuando atmega328pb o similares tengan un oscilador de 32.768 khz (Conectado a XTAL1,2)
    //Este se utiliza tanto para la suspensión como para la generación de los pulsos manchester de manera estable y sin calibrar.
    #define enable_opt //Comentar para deshabilitar la optimización
        //No se recomienda el uso de osciladores internos para la generación de los pulsos manchester, ya que pequeñas variaciones de tensión o temperatura pueden sacar de rango la duración de los periodos de HIGH, LOW de manchester.
    #ifdef enable_opt
        //Librería de optimización
        #include "LowPower.h"
    #endif

// Variable de los pines del emisor RF433 OOK y led
    #define RF433_TX_PIN 8 // Pin de salida de datos hacia el emisor RF433
    #define RF433_VCC 7 // Pin VCC del emisor
    #define LED_PIN 5 // Pin del led que indica la emisión y errores
    #define SHT21_VCC A3 // Pin VCC del sensor

//Variables del mensaje a emitir
    #define V3_PAYLOAD_NIBBLES 15 //Nibbles del payload
    #define V3_TX_BYTES 13

    //Modificar solo si no se usa la optimización
    #ifndef enable_opt
        //Modifica la duración de los periodos de HIGH, LOW de la codificación de manchester.
        //Es útil si el oscilador utilizado no está calibrado o requiere un pequeño ajuste para que el receptor original lo reciba
        #define V3_PULSE_LENGHT_US 488.5  // El receptor original suele aceptar valores entre 487,7 y 489 µs. (Modificar hasta que el receptor capte el sensor).
        #define V3_PULSE_PLUS 20 //Modifica la longitud de los pulsos HIGH, LOW (Puede ser prescindible). Intenta emular el comportamiento visto en un oscilador de un sensor original.
    #endif

// Variables de emulación del sensor
    #define V3_CHANNEL 3 // Canal del sensor (max 15)
    #define V3_Default_INTERVALS_SECS 0 //Establecer 0 para emular los tiempos de emisión de los sensores originales (varian dependiendo del canal), establecer 1 para modificar manualmente el tiempo de emisión
        float V3_TX_INTERVAL_SECS = 60; //Tiempo de emisión del sensor "Solo se aplicará si V3_Default_INTERVALS_SECS es 1"
    #define sensor_id 0xF824 // Identificador del dispositivo
    uint8_t rcode = 0x15; // Rolling Code (Código de emparejamiento): solo modificar el valor cuando el mod_program sea 1,2,3 (Máximo 0xFF)

//Detección de batería baja
static int check_voltaje_batery = 1;
    // 0 --> Deshabilitado (Indicara que la batería está siempre OK)
    // 1 --> Habilitado
    static float vol_low = 2.4; //Establece cuando indicará batería baja (En este caso de los sensores originales es 2.7 v)
    static float vol_calibrate = 0; //Se utiliza para calibrar la referencia interna del arduino al leer el VCC y que coincida con el voltaje real de la batería

// Variables de depuración (Comentarlas para deshabilitarlas). Habilitar solo para testeos (ya que pueden probocar pequeños delays) 
    //#define REGUISTRO_SERIAL
    //#define DEBUG_PAYLOAD
    //#define DEBUG_INC

// Valores default del sensor: temperatura, humedad
    double temp = 25;
    double hum = 50;

//Variables del sht21
SHT21 sht;

// Variables de tendencia de temperatura/humedad
    //Se usa para emular la tendencia de temperatura que aparece en las estaciones receptoras
    uint8_t calculate_trends = 1 ; // 0 Desactivado, 1 activado
    #define num_samples 12 // Número de muestras para el algoritmo
    uint8_t trend_temp = 0; // Tendencia de temperatura: 0 estable, 1 ascendente, 2 descendente (Default)
    uint8_t trend_hum = 0; // Tendencia de humedad: 0 estable, 1 ascendente, 2 descendente (Default)

    float temp_history[num_samples];
    float hum_history[num_samples];
    uint8_t temp_index = 0;
    uint8_t hum_index = 0;
    bool temp_full_buffer = false;
    bool hum_full_buffer = false;
    #define TREND_ESTABLE  0
    #define TREND_UP       1
    #define TREND_DOWN     2

    //Variables para modificar el algoritmo
        float temp_th_estable = 0.05;
        float temp_th_up = 0.08;
        float temp_th_down = -0.08;

        float hum_th_estable = 0.15;
        float hum_th_up = 0.3;
        float hum_th_down = -0.3;

// Estado de la batería del sensor (Default)
    uint8_t battery_status = 0 ;// 0 OK, 1 LOW
    float battery_voltaje = 0;

// Variables del programa
    #ifdef enable_opt
        //Variables de tiempo
        uint32_t V3_TX_INTERVAL_CYCLES = 0; //Tiempo entre emisiones en ciclos de reloj
        uint32_t temp_transmi_cycles = 0; //Tiempo restante en ciclos hasta la siguiente emisión
        uint32_t total_time = 0;
        uint32_t passed_time = 0;
        //Variables de la ISR
        volatile uint32_t trans_time = 0;
        volatile uint8_t timer2_tick = 0;
        //Otras variables
        volatile uint8_t started_transmision = 0;
        uint32_t trans_cycles = 0;
        uint8_t now_sleep = 0;
        uint8_t min_time_sleep = 15; //Indica el tiempo (ms) minimo en el que el arduino se dormera
        uint32_t min_cycles_sleep = (uint32_t)(min_time_sleep * 32.768f + 0.5f);

        //Diccionario de las variables de ajuste del timer2
        typedef struct
        {
            uint8_t  prescaler_bits;
            uint8_t  ocr2a;
            uint32_t nominal_cycles;
        } Timer2Window;
        Timer2Window window = {0,0,0};
        uint8_t last_prescaler_bits = 0;
        uint8_t last_ocr2a = 0;

    #else
        //Variables de tiempo
        float overflows = 0;
        float real_time = 0;
        float beat = 0;
        uint32_t txMicros;
        uint32_t last_millis_trans = 0;
        uint32_t V3_TX_INTERVAL_MSECS_TEMP = 0;
        uint32_t V3_TX_INTERVAL_MSECS = 0;
        bool first_tx = true;
    #endif

    //Variables por defecto
    uint32_t disable_sync_check = 0;
    uint8_t pair_mode = 0;
    uint32_t time_stop_sync = 30UL * 60UL * 1000UL; // Tiempo en el que el modo sync se desactivará (30min por defecto en todos los sensores)
    uint8_t type_pairmode = 0;
    uint8_t error = 0;

// Tiempo de emisión del sensor dependiendo del canal
    struct dic {
        uint8_t n_channel;
        uint8_t time_interval;
    };
    //Tiempo de emisión del sensor original por canales
    struct dic channel_time[] = {
        {1, 53},
        {2, 59},
        {3, 61},
        {4, 67}, //No tested > 3
        {5, 71},
        {6, 79},
        {7, 83},
        {8, 87},
        {9, 91},
        {10, 93},
    };

// -----------------------------------------------------
//Funciones de emulación del protocolo Oregon V3
// -----------------------------------------------------
// calculate a bitwise crc8 ccitt checksum
uint8_t crc8_checksum_v3(uint8_t *payload, uint8_t payloadNibbles) {
    uint8_t crc = 0x00;  // crc8 init value

    // iterate over nibbles with sensor payload including a trailing iteration
    for (uint8_t i = 7; i <= (payloadNibbles + 7); i++) {
        if (i < (payloadNibbles + 7)) {
          if ((i % 2) == 0)
              crc ^= payload[i/2] >> 4;
          else
              crc ^= (payload[i/2] & 0x0F);
        }
        for (uint8_t j = 0; j < 4; j++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else 
                crc <<= 1;
        }
    }
    
    // nibbles need to be swapped (LSD)
    return ((crc & 0x0F) << 4) | ((crc & 0xF0) >> 4);
}

// Esta función se utiliza para esperar entre pulsos de 488us
#ifdef enable_opt
    static void wait_timer2_488us(void) { 
        timer2_tick = 0; 
        while (!timer2_tick) { 
            // Espera activa hasta la siguiente interrupción 
        } 
    }
#endif

// calculate the Oregon V3 checksum
// based on https://github.com/merbanan/rtl_433/blob/master/src/devices/oregon_scientific.c
uint8_t oregon_checksum_v3(uint8_t *payload, int payloadNibbles) {
    uint8_t checksum;
  
    // remove sync byte '0xA' from first sensor data nibble
    checksum = (payload[3] & 0xF);

    // nibbles used for simple checksum (sum of nibbles)
    // 4 (sensor id) + 1 (V3_CHANNEL) + 2 (rolling code) + 1 (flag) + payloadNibbles
    for (uint8_t i = 4; i < ((28 + payloadNibbles*4) / 8); i++)
        checksum += (payload[i] >> 4) + (payload[i] & 0xF);

    if ((payloadNibbles % 2) == 0)
        checksum += payload[((28 + payloadNibbles*4)/8)] >> 4;

    return ((checksum & 0x0F) << 4) | ((checksum & 0xF0) >> 4);
}

// Send a single byte as a series of Manchester encoded RF pulses.
// Bit order: 4..7 followed by 0..3.
//
// With enable_opt:
//   HIGH/LOW duration = 488.28125 us
//   Timer2 provides the timing from the 32.768 kHz crystal.
//
// Without enable_opt:
//   Original micros()/delayMicroseconds() timing is used.
//

void manchester_encode_v3(uint8_t txByte){
    uint8_t bitMask = 0;

    // Send 8 bits of data
    for (uint8_t i = 0; i < 8; i++)    {
        #ifndef enable_opt
            txMicros += (uint32_t)(V3_PULSE_LENGHT_US * 2);
            int32_t diff = (int32_t)(txMicros - micros());
            if (diff > 0)
                delayMicroseconds((unsigned int)diff);
        #endif

        // Send bit 4..7 first, followed by bit 0..3
        if (!bitMask)
            bitMask = 0x10;          // start with bit 4
        else if (bitMask == 0x80)
            bitMask = 0x01;          // jump from bit 7 to bit 0
        else
            bitMask <<= 1;            // next bit

        // High bit = ON-to-OFF
        // Low bit  = OFF-to-ON
        uint8_t level = (txByte & bitMask) ? HIGH : LOW;

        digitalWrite(RF433_TX_PIN, level);

        #ifdef enable_opt
            // First half of Manchester bit
            wait_timer2_488us();
        #else
            // First half of Manchester bit
            delayMicroseconds(V3_PULSE_LENGHT_US + V3_PULSE_PLUS);
        #endif
                digitalWrite(RF433_TX_PIN, !level);
        #ifdef enable_opt
                // Second half of Manchester bit
                wait_timer2_488us();
        #endif
    }
}

// returns payload for THGN801 outdoor sensor (13 bytes)
uint8_t *payload_thgn801(uint8_t rcode, uint8_t pair_mode, uint8_t battery_low, uint8_t trend_temp, float tempC, uint8_t hum, uint8_t trend_hum) {
    static uint8_t payload[V3_TX_BYTES];
    uint16_t t10;
    // 6 nibbles preamble
    // 1 nibble sync
    // 4 nibbles sensor id
    // 1 nibble channel
    // 2 nibbles rolling code
    // 1 nibble flag (battery)
    // n nibbles sensor specific payload (THGN801 n=7, 28 bits)
    // 2 nibbles oregon checksum
    // 2 nibbles crc8 checksum
    memset(payload, 0, V3_TX_BYTES);
  
    // preamble with 1-bit pulses (6 nibbles) and 1 sync nibble '0101' (28 bits)
    memset(payload, 0xFF, 3);
    payload[3] = 0xA0;
  
    // nibbles 8..11: oregon sensor id (16 bits)
    payload[3] |= (sensor_id >> 12) & 0x0F;  // A = bits 15..12
    payload[4] = (sensor_id >> 4) & 0xFF;   // BC = bits 11..4
    payload[5] = (sensor_id & 0x0F) << 4;         // D = bits 3..0
  
    // nibble 12: channel 1..10 for THGN801 using a WMR200 base station (4 bits)
    payload[5] |= V3_CHANNEL > 15 ? 15 : V3_CHANNEL;
  
    // nibble 13..14 is rolling code, changes on sensor reset (16 bits LSD) 
    payload[6] = rcode;
  
    // nibble 15 Es el encargado de indicar:
        // 0x00 --> Indica Batería OK, sync deshabilitado, temperatura estable
        // 0x10 --> Indica Batería OK, sync deshabilitado, temperatura ascendente
        // 0x20 --> Indica Batería OK, sync deshabilitado, temperatura descendente
        // 0x30 --> (No usado por el sensor) Indica Batería OK, sync deshabilitado, (la fecha de temperatura de la consola oregon, desaparece)
        // 0x40 --> Indica Batería low, sync deshabilitado, temperatura estable
        // 0x50 --> Indica Batería low, sync deshabilitado, temperatura ascendente
        // 0x60 --> Indica Batería low, sync deshabilitado, temperatura descendente
        // 0x70 --> (No usado por el sensor) Indica Batería low, sync deshabilitado, (la fecha de temperatura de la consola oregon, desaparece)
        // 0x80 --> Indica Batería OK, sync habilitado, temperatura estable
        // 0x90 --> Indica Batería OK, sync habilitado, temperatura ascendente
        // 0xA0 --> Indica Batería OK, sync habilitado, temperatura descendente
        // 0xB0 --> (No usado por el sensor) Indica Batería OK, sync habilitado, (la fecha de temperatura de la consola oregon, desaparece)
        // 0xC0 --> Indica Batería low, sync habilitado, temperatura estable
        // 0xD0 --> Indica Batería low, sync habilitado, temperatura ascendente
        // 0xE0 --> Indica Batería low, sync habilitado, temperatura descendente
    int V3_nibble15 = (pair_mode * 8 + battery_low * 4 + trend_temp) * 0x10;
    payload[7] = V3_nibble15;

    // nibbles 16..22 encode THGN801 specific data (24 bits)
    // 16..19 temperature in degC as LSD with 0.1 precision
    t10 = abs(tempC * 10.001);  // .001 required to fix rounding issues
    payload[7] |= ((t10 % 10) & 0x0F);
    payload[8] = ((t10 / 10) % 10) << 4 | ((t10 / 100) & 0x0F);
    payload[9] = (tempC < 0.0) ? 0x80 : 0; // nibble 19 encodes temperature sign (0x80 => neg.)    

    // 20..21 encodes relative humidity
    uint8_t h = (uint8_t)round(hum);
    payload[9] |= (h % 10) & 0x0F; // 0-100%
    payload[10] = ((h / 10) % 10) << 4;
    // 22 Es usado para indicar la tendencia de la humedad
        // 0x0 --> humedad estable
        // 0x1 --> humedad ascendente
        // 0x2 --> Humedad descendente
        // 0x3 --> (No usado por el sensor), la tendencia no aparece en el receptor original
    payload[10] |= trend_hum * 0x1;

    // checksum (8 bits) for nibbles 16..21 with sensor specific data (sum of nibbles)
    payload[11] = oregon_checksum_v3(payload, V3_PAYLOAD_NIBBLES);

    // crc8 checksum for V3 protocol (8 bits)
    payload[12] = crc8_checksum_v3(payload, V3_PAYLOAD_NIBBLES);

    return payload;  
}


// send manchester encoded payload for a Oregon V3 sensor
void send_data_v3(uint8_t *payload, uint8_t len) {
    uint8_t i;
    #ifdef REGUISTRO_SERIAL
      #ifdef DEBUG_PAYLOAD
          Serial.print(millis());
          Serial.print(": V3 Payload [ ");
          for (i = 0; i < len; i++) {
              if (payload[i] < 16)
                  Serial.print("0");
              Serial.print(payload[i], HEX);
              Serial.print(" ");
          }
          Serial.println("]");
      #endif
    #endif
    digitalWrite(RF433_TX_PIN, LOW);
    #ifdef enable_opt
        wait_timer2_488us();
    #else
        txMicros = micros();
    #endif
    for (i = 0; i < len; i++)
        manchester_encode_v3(payload[i]);

    // need to add extra delay after last low to high 
    // pulse since there is no more data to come...
    if ((payload[len - 1] & 0x08) == 0) //Testeando
        #ifdef enable_opt
            wait_timer2_488us();
        #else
            delayMicroseconds(V3_PULSE_LENGHT_US);
        #endif
    digitalWrite(RF433_TX_PIN, LOW);
}

// ---------------------------------
// Función de medición de la batería
// ---------------------------------
//Mide el voltaje de la batería y indica batería baja en caso de que este por debajo de lo establecido
// Devuelve el VCC en voltios (p. ej. 3.29).
float readVcc() {
  // Referencia = VCC, entrada = bandgap interno de 1.1 V
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  ADCSRA |= _BV(ADEN);          // por si el ADC sigue apagado tras la suspensión
  delay(2);                     // estabiliza el bandgap

  // Descarta la primera conversión tras cambiar de referencia
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC));

  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC));
  uint16_t raw = ADC;
  if (raw == 0) return 0;       // evita dividir entre 0

  float mv = 1125300.0f / raw;  // 1.1 V * 1023 * 1000
  return (mv / 1000.0f);
}

// --------------------------------------------------------
//Funciones del cálculo de tendencia de temperatura/humedad
// --------------------------------------------------------
float average_cal(float history[])
{
    float sum = 0.0;
    for (int i = 0; i < num_samples; i++)
        sum += history[i];
    return sum / num_samples;
}

// ============================================================
// Calcula tendencia mediante regresión lineal.
//
// Devuelve:
//   0 = estable
//   1 = subida
//   2 = bajada
//
// history[]           Buffer circular
// index               Índice de escritura
// trend               Tendencia anterior
// full_buffer         TRUE cuando el buffer está lleno
// TH_ESTABLE          Umbral de estabilidad / minuto
// TH_UP               Umbral de subida / minuto
// TH_DOWN             Umbral de bajada / minuto
// value               Nueva medida
// sample_interval_sec Intervalo entre muestras en segundos
//
// La pendiente resultante se expresa en:
//
//   unidades / minuto
//
// Por tanto, los umbrales son independientes de la frecuencia
// de muestreo.
// ============================================================

uint8_t cal_trend(
    float history[],
    uint8_t &index,
    uint8_t &trend,
    bool &full_buffer,
    float TH_ESTABLE,
    float TH_UP,
    float TH_DOWN,
    float value,
    float sample_interval_sec)
{
    // --------------------------------------------------------
    // Redondear a una decimal
    // --------------------------------------------------------

    value = roundf(value * 10.0f) / 10.0f;


    // --------------------------------------------------------
    // Guardar muestra
    // --------------------------------------------------------

    history[index] = value;

    index++;

    if (index >= num_samples)
    {
        index = 0;
        full_buffer = true;
    }


    // --------------------------------------------------------
    // No tenemos suficientes muestras todavía
    // --------------------------------------------------------

    if (!full_buffer)
    {
        trend = TREND_ESTABLE;
        return trend;
    }


    // --------------------------------------------------------
    // Protección
    // --------------------------------------------------------

    if (sample_interval_sec <= 0.0f)
    {
        trend = TREND_ESTABLE;
        return trend;
    }


    // --------------------------------------------------------
    // Regresión lineal
    //
    // x = 0, 1, 2, ..., N-1
    //
    // Como el intervalo entre muestras es constante,
    // podemos utilizar simplemente el número de muestra
    // como eje X.
    // --------------------------------------------------------

    float sum_x  = 0.0f;
    float sum_y  = 0.0f;
    float sum_xy = 0.0f;
    float sum_x2 = 0.0f;

    for (uint8_t i = 0; i < num_samples; i++)
    {
        // index apunta a la muestra más antigua

        uint8_t pos = (index + i) % num_samples;

        float x = (float)i;
        float y = history[pos];

        sum_x  += x;
        sum_y  += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }


    // --------------------------------------------------------
    // Calcular pendiente
    //
    // Resultado inicial:
    //
    //   unidades / muestra
    // --------------------------------------------------------

    float n = (float)num_samples;

    float denominator =
        (n * sum_x2) - (sum_x * sum_x);

    if (denominator == 0.0f)
    {
        trend = TREND_ESTABLE;
        return trend;
    }

    float slope_per_sample =
        ((n * sum_xy) - (sum_x * sum_y))
        / denominator;


    // --------------------------------------------------------
    // Convertir a unidades / minuto
    //
    // sample_interval_sec:
    //
    // 10 s  -> 6 muestras/min
    // 30 s  -> 2 muestras/min
    // 60 s  -> 1 muestra/min
    // --------------------------------------------------------

    float samples_per_minute =
        60.0f / sample_interval_sec;

    float slope_per_minute =
        slope_per_sample * samples_per_minute;


    // --------------------------------------------------------
    // Determinar tendencia con histéresis
    // --------------------------------------------------------

    switch (trend)
    {
        // ====================================================
        // ESTABLE
        // ====================================================

        case TREND_ESTABLE:

            if (slope_per_minute >= TH_UP)
            {
                trend = TREND_UP;
            }
            else if (slope_per_minute <= TH_DOWN)
            {
                trend = TREND_DOWN;
            }

            break;


        // ====================================================
        // SUBIENDO
        // ====================================================

        case TREND_UP:

            // Sigue subiendo
            if (slope_per_minute >= TH_ESTABLE)
            {
                trend = TREND_UP;
            }

            // Ha entrado en zona estable
            else if (slope_per_minute > TH_DOWN)
            {
                trend = TREND_ESTABLE;
            }

            // Cambio claro a bajada
            else
            {
                trend = TREND_DOWN;
            }

            break;


        // ====================================================
        // BAJANDO
        // ====================================================

        case TREND_DOWN:

            // Sigue bajando
            if (slope_per_minute <= -TH_ESTABLE)
            {
                trend = TREND_DOWN;
            }

            // Ha entrado en zona estable
            else if (slope_per_minute < TH_UP)
            {
                trend = TREND_ESTABLE;
            }

            // Cambio claro a subida
            else
            {
                trend = TREND_UP;
            }

            break;
    }

    return trend;
}

// ------------------------------------------------
// Funciones de optimización de consumo de energía con el oscilador de 32Khz
// ------------------------------------------------
#ifdef enable_opt

    // Obtiene el prescaler/OCR2A más apropiados
    // para el tiempo solicitado, expresado en ciclos
    // de 32.768 kHz.
    Timer2Window Timer2_GetBestWindow(uint32_t cycles)
    {
        static const uint16_t prescaler[7] = {
            1, 8, 32, 64, 128, 256, 1024
        };

        static const uint8_t bits[7] = {
            (1 << CS20),
            (1 << CS21),
            (1 << CS21) | (1 << CS20),
            (1 << CS22),
            (1 << CS22) | (1 << CS20),
            (1 << CS22) | (1 << CS21),
            (1 << CS22) | (1 << CS21) | (1 << CS20)
        };

        Timer2Window best = { 0, 0, 0 };

        /* Máximo que puede generar Timer2:
        256 ticks × 1024 = 262144 ciclos */
        if (cycles >= 262144UL)
        {
            best.prescaler_bits =
                (1 << CS22) | (1 << CS21) | (1 << CS20);

            best.ocr2a = 255;
            best.nominal_cycles = 262144UL;

            return best;
        }

        uint32_t best_error = UINT32_MAX;

        for (uint8_t i = 0; i < 7; i++)
        {
            /*
            * Número de ticks Timer2 necesarios.
            * Redondeo al entero más próximo.
            */
            uint32_t ticks =
                (cycles + prescaler[i] / 2) / prescaler[i];

            /* Timer2 permite 1..256 ticks */
            if (ticks == 0)
                ticks = 1;

            if (ticks > 256)
                ticks = 256;

            uint32_t nominal_cycles =
                ticks * prescaler[i];

            uint32_t error =
                (nominal_cycles > cycles)
                ? (nominal_cycles - cycles)
                : (cycles - nominal_cycles);

            if (error < best_error)
            {
                best_error = error;

                best.prescaler_bits = bits[i];
                best.ocr2a = (uint8_t)(ticks - 1);
                best.nominal_cycles = nominal_cycles;
            }
        }

        return best;
    }

    //Establece el prescaler y el ocr2a
    void Timer2_SetWindow(uint8_t prescaler_bits, uint8_t ocr2a_put, uint8_t &last_prescaler_bits, uint8_t &last_ocr2a)
    {

        /* Reiniciar contador si cambia la configuración */
        if ((prescaler_bits != last_prescaler_bits) ||
            (ocr2a_put != last_ocr2a))
        {
            TCNT2 = 0;

            while (ASSR & (1 << TCN2UB));
        }

        /* Cambiar OCR2A solamente si ha cambiado */
        if (ocr2a_put != last_ocr2a)
        {
            OCR2A = ocr2a_put;

            while (ASSR & (1 << OCR2AUB));
        }

        /* Cambiar prescaler solamente si ha cambiado */
        if (prescaler_bits != last_prescaler_bits)
        {
            TCCR2B = prescaler_bits;

            while (ASSR & (1 << TCR2BUB));
        }

        /* Guardar configuración actual para la próxima llamada */
        last_prescaler_bits = prescaler_bits;
        last_ocr2a = ocr2a_put;
    }

    //Establece el prescaler/ocr2a para emitir una interupción cada 488us
    void set_emision_period_488us(){
        uint8_t prescaler_put = (1 << CS21);
        uint8_t ocr2a_put = 1;
        Timer2_SetWindow(prescaler_put, ocr2a_put, last_prescaler_bits, last_ocr2a);
    }

    // Rutina de Servicio de Interrupción (ISR)
    ISR(TIMER2_COMPA_vect) { 
        if (started_transmision){
            trans_time ++;
        }
        timer2_tick = 1; 
    }
#endif

// ---------------------------------------
// Configuración inicial
// ---------------------------------------
void setup() {
    //Config preliminar
    pinMode(RF433_TX_PIN, OUTPUT);
    pinMode(RF433_VCC, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(6, OUTPUT);
    digitalWrite(6, LOW);
    pinMode(SHT21_VCC, OUTPUT);

    #ifdef REGUISTRO_SERIAL
      Serial.begin(38400);
    #endif

    // Calcula el tiempo de emisión dependiendo del canal, si esta activo
    if (V3_Default_INTERVALS_SECS == 0){
        for (int i = 0; i < sizeof(channel_time) / sizeof(channel_time[0]); i++) {
            if (channel_time[i].n_channel == V3_CHANNEL) {
                V3_TX_INTERVAL_SECS = channel_time[i].time_interval;
            }
        }
    }

    #ifdef enable_opt
        //Configuración inicial del oscilador/timer2
        cli();
        TIMSK2 = 0; TCCR2A = 0; TCCR2B = 0;
        ASSR |= (1 << AS2);
        TCNT2 = 0; OCR2A = 1;
        TCCR2A = (1 << WGM21);                       // CTC
        while (ASSR & ((1<<TCN2UB)|(1<<OCR2AUB)|(1<<TCR2AUB)|(1<<TCR2BUB)));
        TIFR2  = (1<<OCF2A)|(1<<OCF2B)|(1<<TOV2);
        TIMSK2 = (1 << OCIE2A);                      // compare match, no overflow
        sei();
    #endif

    //Configuración general
    //Tipo de programa
    switch (mod_program){
        case 0: // Emulacion 100% original al sensor (modo emparejamiento durante 30m tras el inicio del programa y rolling code varía en cada reset/apagado)
            randomSeed(analogRead(A0) ^ micros()); // random seed for rolling code
            rcode = random(0x01, 0xFE);
            type_pairmode = 1;
            break;
        case 1: // Emulacion con modo emparejamiento (durante 30m tras el inicio del programa) y rolling code fijo
            type_pairmode = 1;
            break;
        case 2: // Emulacion con modo emparejamiento siempre activo y rolling code fijo (Identificador unico)
            type_pairmode = 2;
            break;
        case 3: // Emulacion con modo emparejamiento desactivado y rolling code fijo
            type_pairmode = 0;
            break;
        case 4: // Emulacion con modo emparejamiento desactivado y rolling code variable (No se para que se usaría)
            randomSeed(analogRead(A0) ^ micros()); // random seed for rolling code
            rcode = random(0x01, 0xFE);
            type_pairmode = 0;
            break;
        default: // Error, establece mod_program un valor correcto
            error = 1;
            break;
    }

    #ifdef enable_opt
        //Convierte los segundos de emisión a ciclos del oscilador de 32Khz
        V3_TX_INTERVAL_CYCLES = (uint32_t)V3_TX_INTERVAL_SECS * 32768UL;
    #else
        V3_TX_INTERVAL_MSECS = (uint32_t)(V3_TX_INTERVAL_SECS * 1000.0f);
    #endif
    
    #ifdef REGUISTRO_SERIAL
        Serial.print(F("\n\nStarting Oregon V3 THGN801 emulator...\n\n"));
        if (error == 1){
            Serial.println("ERROR, establece la variable mod_program bien");
            Serial.print("Parando el programa ...");
        } else {
            Serial.print("Modo de programa seleccionado: ");
            Serial.print(mod_program);
            Serial.print(", Rolling code: ");
            Serial.print(rcode, HEX);
            Serial.print(", Channel: ");
            Serial.print(V3_CHANNEL);
            Serial.print(", Transmission interval: ");
            Serial.print(V3_TX_INTERVAL_SECS);
            Serial.print(", RF433 packets: ");
            Serial.println(V3_TX_BYTES);
        }
    #endif

    if (error == 1){
        while (1) {
                digitalWrite(LED_PIN, HIGH);
                delay(250);
                digitalWrite(LED_PIN, LOW);
                delay(250);
            }
    }
}

void loop() {
    #ifdef enable_opt
    if (temp_transmi_cycles > window.nominal_cycles){
        temp_transmi_cycles = temp_transmi_cycles - window.nominal_cycles;
    } else {
        temp_transmi_cycles = 0;
    }

    // Configuración de envío del mensaje
    if (temp_transmi_cycles == 0){
        //Variables de emisión
            //Tiempo hasta la siguiente emisión
            temp_transmi_cycles = V3_TX_INTERVAL_CYCLES;
            //Pone a 0 el número de pulsos de 488us
            trans_time = 0;
            trans_cycles = 0; //Conversión a ciclos de pulsos
            //Indica que se ha iniciado la transmisión
            started_transmision = 1;
            //Aumenta el contador de total_time
            total_time += (uint64_t)V3_TX_INTERVAL_CYCLES * 1000ULL / 32768ULL;
        //Establece la configuración del timer2 para la emisión
        set_emision_period_488us();
    #else
    uint32_t now = millis();
    if (first_tx || (uint32_t)(now - last_millis_trans) >= V3_TX_INTERVAL_MSECS) {
        first_tx = false;
        last_millis_trans = now;
    #endif

        //Encargado de contar los 30m para el sync_mode "emparejamiento"
        switch (type_pairmode){
            case 0:
                pair_mode = 0;
                break;
            case 1:
                if (disable_sync_check == 0) {
                    #ifdef enable_opt
                    if (total_time < time_stop_sync) {
                    #else
                    if (millis() < time_stop_sync){
                    #endif
                        pair_mode = 1;
                    } else {
                        pair_mode = 0;
                        disable_sync_check = 1;
                    }
                }
                break;
            case 2:
                pair_mode = 1;
                break;
        }
        
        //Comprueba el voltaje de la batería
        if (check_voltaje_batery){
            battery_voltaje = readVcc() + vol_calibrate;
            if (battery_voltaje > vol_low){
                battery_status = 0;
            } else {
                battery_status = 1;
            }
        }

        //Lee el sensor SHT21
        #ifdef enable_opt
            //Habilita el SDA y SCL tras la suspensión
            TWCR = 0;
            Wire.begin();
        #endif
        digitalWrite(SHT21_VCC, HIGH);
        sht.reset();
        //Obtiene las lecturas del sensor
        temp = sht.getTemperature();
        hum = sht.getHumidity();
        #ifdef enable_opt
            Wire.end();
        #endif
        digitalWrite(SHT21_VCC, LOW);

        //Calcula la tendencia de temperatura/Humedad
        if (calculate_trends == 1){
            cal_trend(temp_history, temp_index, trend_temp, temp_full_buffer, temp_th_estable, temp_th_up, temp_th_down, temp, V3_TX_INTERVAL_SECS);
            cal_trend(hum_history, hum_index, trend_hum, hum_full_buffer, hum_th_estable, hum_th_up, hum_th_down, hum, V3_TX_INTERVAL_SECS);
        }

        //Enciende el led de referencia
        digitalWrite(LED_PIN, HIGH);
        delay(20);
        digitalWrite(LED_PIN, LOW);

        //Alimenta el RF433
        digitalWrite(RF433_VCC, HIGH);
        //Crea y envia el mensaje
        send_data_v3(payload_thgn801(rcode, pair_mode, battery_status, trend_temp, temp, hum, trend_hum), V3_TX_BYTES);
        digitalWrite(RF433_VCC, LOW);
        
        #ifdef REGUISTRO_SERIAL
            Serial.print("THGN801 Data transmitted: ");
            Serial.print("Sync mode: ");
            if (pair_mode == 1){
              Serial.print("Enabled");
            } else {
              Serial.print("Disable");
            };
            Serial.print(", Battery status: ");
            if (battery_status == 0){
                Serial.print("Ok");
            }else if (battery_status == 1){
                Serial.print("Low");
            }
            Serial.print(", (Temperature: ");
            Serial.print(temp);
            switch (trend_temp) {
                case 0: // Estable
                    Serial.print("\xE2\x80\x94"); // EM DASH            
                    break;
                case 1: // Ascendente
                    Serial.print("\xE2\x86\x91"); // UP ARROW
                    break;
                case 2: // Descendente
                    Serial.print("\xE2\x86\x93"); // DOWN ARROW
                    break;
                default:
                    Serial.print("\xE2\x80\x94"); // EM DASH            
                    break;
            }
            Serial.print(" °C, Humidity: ");
            Serial.print(hum);
            switch (trend_hum) {
                case 0: // Estable
                    Serial.print("\xE2\x80\x94"); // EM DASH            
                    break;
                case 1: // Ascendente
                    Serial.print("\xE2\x86\x91"); // UP ARROW
                    break;
                case 2: // Descendente
                    Serial.print("\xE2\x86\x93"); // DOWN ARROW
                    break;
                default:
                    Serial.print("\xE2\x80\x94"); // EM DASH            
                    break;
            }
            Serial.print(" %");
            Serial.println(")");
            Serial.println();
        #endif
        #ifdef DEBUG_INC
                // increase readings for testing
                temp += 1;
                if (temp  > 50)
                    temp = 15;
                if (calculate_trends == 0){
                    trend_temp += 1;
                    if (trend_temp > 3){
                        trend_temp = 0;
                    }
                }
                hum += 1;
                if (hum > 95)
                    hum = 0;
                if (calculate_trends == 0){
                    trend_hum += 1;
                    if (trend_hum > 3)
                        trend_hum = 0;
                }
        #endif
    #ifdef enable_opt
        // Obtiene el tiempo que ha tardado en enviar el mensaje
        //Para el contador de ciclos durante la emisión
        started_transmision = 0;
        // trans_time = número de pulsos de 488,28125 us.
        trans_cycles = trans_time * 16UL;
        //Se calcula el tiempo restante para la siguiente emisión
        temp_transmi_cycles = temp_transmi_cycles - trans_cycles;
        //Se obtiene la siguiente ventana del periodo de suspensión
        window = Timer2_GetBestWindow(temp_transmi_cycles);
        //Se establece los parámetros para el timer2
        Timer2_SetWindow(window.prescaler_bits, window.ocr2a, last_prescaler_bits, last_ocr2a);
        //Si el tiempo que debe estar suspendido es menor a x
        if (window.nominal_cycles < min_cycles_sleep){
            //Espera a que pase el pulso
            timer2_tick = 0;
            while (!timer2_tick) {}
            now_sleep = 0;
        } else {
            now_sleep = 1;
        }
    } else {
        //Obtiene el siguiente periodo de suspensión
        window = Timer2_GetBestWindow(temp_transmi_cycles);
        Timer2_SetWindow(window.prescaler_bits, window.ocr2a, last_prescaler_bits, last_ocr2a);
        //Si el tiempo que debe estar suspendido es menor a x
        if (window.nominal_cycles < min_cycles_sleep){
            //Espera a que pase el pulso
            timer2_tick = 0;
            while (!timer2_tick) {}
            now_sleep = 0;
        } else {
            now_sleep = 1;
        }
    #endif
    }
    #ifdef enable_opt
        if (now_sleep){
            #ifdef REGUISTRO_SERIAL
                Serial.flush();
            #endif
            //Configuración de optimización
            LowPower.powerSave(SLEEP_FOREVER, ADC_OFF, BOD_OFF, TIMER2_ON);
        }
    #endif   
}
