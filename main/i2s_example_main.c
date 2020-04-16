/* I2S Example

	This example code will output 100Hz sine wave and triangle wave to 2-channel of I2S driver
	Every 5 seconds, it will change bits_per_sample [16, 24, 32] for i2s data

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2s.h"
#include "esp_system.h"
#include <math.h>
#include <string.h>


#define SAMPLE_RATE	 (48000)
#define I2S_NUM		 (0)
#define WAVE_FREQ_HZ	(657)
#define PI			  (3.14159265)
#define I2S_BCK_IO	  (GPIO_NUM_16)
#define I2S_WS_IO	   (GPIO_NUM_21)
#define I2S_DI_IO	   (GPIO_NUM_19)
#define I2S_DO_IO	   (GPIO_NUM_17)
#define BITS_PER_SAMPLE		32
#define BYTES_PER_SAMPLE	(32/8)
#define NUM_CHANNELS		2
#define NUM_SAMPLES	480

#define SAMPLE_PER_CYCLE (SAMPLE_RATE/WAVE_FREQ_HZ)

#define GPIO_OUTPUT_IO_0	18
#define GPIO_OUTPUT_IO_1	4
#define GPIO_OUTPUT_IO_2	23
#define GPIO_OUTPUT_IO_3	22
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1) | (1ULL<<GPIO_OUTPUT_IO_2))

int activeXferBuffer = 0;
int sampleData[2][NUM_SAMPLES * BYTES_PER_SAMPLE * NUM_CHANNELS];

// make it so I can change between double and float
#undef USE_DOUBLE
#ifdef USE_DOUBLE
typedef double myFloat_t;
#define SIN sin
#define FMOD_ fmod 
#else
typedef double myFloat_t;
#define SIN sinf
#define FMOD_ fmodf
#endif

int phaseIndex = 0;
myFloat_t phase = 0.0;
myFloat_t normalizedPhase=0.0;
myFloat_t phaseStep = ((2 * PI) * (myFloat_t)WAVE_FREQ_HZ / (myFloat_t)SAMPLE_RATE);
myFloat_t triangle_step = (myFloat_t) pow(2, 15) / SAMPLE_PER_CYCLE;
myFloat_t triangleFloat = 0.0;

static void setup_triangle_sine_waves(int bufferNum)
{
	unsigned int i;

	myFloat_t sinFloat;
//	myFloat_t testFloat;
	int tempLeft, tempRight;

#undef PRINT_IT
#ifdef PRINT_IT
	printf("\r\nTest bits=%d free mem=%d, written data=%d\n", BITS_PER_SAMPLE, esp_get_free_heap_size(), NUM_SAMPLES * BYTES_PER_SAMPLE * NUM_CHANNELS);
#endif



	for(i = 0; i < NUM_SAMPLES; i++) {
//		gpio_set_level(GPIO_OUTPUT_IO_3, 1);
//		sinFloat = SIN(phaseIndex++ * (2 * PI) * (myFloat_t)WAVE_FREQ_HZ / (myFloat_t)SAMPLE_RATE);
		sinFloat = SIN(phase);
		phase += phaseStep;
//		gpio_set_level(GPIO_OUTPUT_IO_3, 0);
//		sinFloat *=  32767.0;
//		testFloat *=  32767.0;

#ifdef PRINT_IT
		printf("%f,\t%f,\t", sinFloat, triangleFloat);
#endif
//		sinFloat *= (pow(2, BITS_PER_SAMPLE)/2 - 1);

//		sampleData[i*2] = ((int) triangleFloat & 0xffff0000);
//		sampleData[i*2] = ((int) sinFloat & 0xffff0000);
//		sampleData[i*2 + 1] = ((int) sinFloat & 0xffff0000);

//		normalizedPhase = FMOD_(phase, (2 * PI));

		if ((normalizedPhase < (PI/2)) || (normalizedPhase > (3*PI/2)))
		{
			triangleFloat += triangle_step;
		}
		else
		{
			triangleFloat -= triangle_step;
		}

		tempLeft = (((unsigned int)triangleFloat) << 16);
//		tempRight = (((unsigned int) sinFloat) << 16);
		tempRight = (((unsigned int)triangleFloat) << 16);

		sampleData[bufferNum][i*2] = tempLeft;
		sampleData[bufferNum][i*2 + 1] = tempRight;
		if (tempLeft == 0)
			printf("%f,\t%f,\t", sinFloat, triangleFloat);
		
#ifdef PRINT_IT
		printf("%5u,\t%5u,\t%5d,\t%5d\r\n",
				(unsigned short)(sampleData[i*2]>>16),
				(unsigned short)(sampleData[i*2+1]>>16),
				(sampleData[i*2]>>16),
				(sampleData[i*2+1]>>16)
				);
#endif

	}
}


void i2sDataGenerateTask(void* params)
{
	SemaphoreHandle_t syncData = *((SemaphoreHandle_t *)params) ;

//	xSemaphoreTake(syncData, (TickType_t) 200);
	while (1)
	{
		if(xSemaphoreTake(syncData, (TickType_t) 200))
		{
			gpio_set_level(GPIO_OUTPUT_IO_1, 1);
			setup_triangle_sine_waves(activeXferBuffer ^ 1);
			gpio_set_level(GPIO_OUTPUT_IO_1, 0);
		}
	}
}
void i2sTransferTask(void* params)
{
	int currentBuffer;
	SemaphoreHandle_t syncData = *((SemaphoreHandle_t *)params) ;
	unsigned int i2s_bytes_written;

	gpio_set_level(GPIO_OUTPUT_IO_2, 1);
	setup_triangle_sine_waves(0);
	setup_triangle_sine_waves(1);
	gpio_set_level(GPIO_OUTPUT_IO_2, 0);

	vTaskDelay(100/portTICK_RATE_MS);

	// blast data to it
	while (1)
	{
		gpio_set_level(GPIO_OUTPUT_IO_0, 1);
		currentBuffer = activeXferBuffer;
		activeXferBuffer ^= 1;
		xSemaphoreGive(syncData);
		i2s_write(I2S_NUM, &sampleData[currentBuffer], NUM_SAMPLES * BYTES_PER_SAMPLE * NUM_CHANNELS, &i2s_bytes_written, 300);
#ifdef PRINT_IT
		printf("bytes written %d at %d\r\n", i2s_bytes_written, xTaskGetTickCount());
#endif
		gpio_set_level(GPIO_OUTPUT_IO_0, 0);
	}

}

void app_main(void)
{
	BaseType_t retVal;
	QueueHandle_t* i2sQueue;
	SemaphoreHandle_t syncData;

	// sample rate is 48000
	// It's 32 bit per sample because the mic I'm using does this
	// we can just populate the first 16 bits
	i2s_config_t i2s_config = {
		.mode = I2S_MODE_MASTER | I2S_MODE_TX,
		.sample_rate = SAMPLE_RATE,
		.bits_per_sample = BITS_PER_SAMPLE,
		.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,						   //2-channels
		.communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
		.dma_buf_count = 32,
		.dma_buf_len = 960,
		.use_apll = false,
		.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1								//Interrupt level 1
	};

	i2s_pin_config_t pin_config = {
		.bck_io_num = I2S_BCK_IO,
		.ws_io_num = I2S_WS_IO,
		.data_out_num = I2S_DO_IO,
		.data_in_num = I2S_DI_IO											   //NOT USED
	};

	gpio_config_t io_conf = {
		.intr_type = GPIO_PIN_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = GPIO_OUTPUT_PIN_SEL,
		.pull_down_en = 0,
		.pull_up_en = 0
	};
	gpio_config(&io_conf);
	vTaskDelay(100/portTICK_RATE_MS);
	gpio_set_level(GPIO_OUTPUT_IO_0, 1);
	gpio_set_level(GPIO_OUTPUT_IO_1, 1);
	gpio_set_level(GPIO_OUTPUT_IO_2, 1);

	gpio_set_level(GPIO_OUTPUT_IO_0, 0);
	gpio_set_level(GPIO_OUTPUT_IO_1, 0);
	gpio_set_level(GPIO_OUTPUT_IO_2, 0);

 
	// get the buffer ready with some canned waveforms
//	sampleData = malloc(NUM_SAMPLES * BYTES_PER_SAMPLE * NUM_CHANNELS);

	// get the i2s port running
	i2s_driver_install(I2S_NUM, &i2s_config, 20, &i2sQueue);
	i2s_set_pin(I2S_NUM, &pin_config);

	syncData = xSemaphoreCreateBinary();

	memset(sampleData, 0, sizeof(sampleData));

	vTaskSuspendAll();

	// Start a task to handle i2s Data Generation to feed the transfer task
	if ((retVal = xTaskCreate(i2sDataGenerateTask, "i2s Generate Data", 8192, (void *)&syncData, 3, NULL)) != pdPASS)
	{
		printf("task create failed\n");
	}
	
	// Start a task to handle i2s Transfer
//	if ((retVal = xTaskCreate(i2sTransferTask, "i2s Transfer", configMINIMAL_STACK_SIZE, (void *)i2sQueue, 2, NULL)) != pdPASS)
	if ((retVal = xTaskCreate(i2sTransferTask, "i2s Transfer", 8192, (void *)&syncData, 2, NULL)) != pdPASS)
	{
		printf("task create failed\n");
	}

	xTaskResumeAll();

	// start the transfer task first
	xSemaphoreGive(syncData);
//	vTaskResume(i2sDataGenerateTask);
//	vTaskDelay(100/portTICK_RATE_MS);
//	vTaskResume(i2sTransferTask);

	while (1)
		vTaskDelay(5000/portTICK_RATE_MS);
//	QueueHandle_t* i2sQueue = (QueueHandle_t *)params;
	i2s_event_t i2sEvent;

	printf("Queue is %08x\r\n", (int)i2sQueue);
	while (true) {
		printf("Waiting on i2sQueue\r\n");
		if (xQueueReceive(*i2sQueue, &i2sEvent, (TickType_t)0))
		{
			printf("%d %d\r\n", i2sEvent.type, i2sEvent.size);
		}
	}
}
