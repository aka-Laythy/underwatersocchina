#include "DCMotor.h"
#include "ch32v30x.h"

#define DCMOTOR_TIM         TIM4
#define DCMOTOR_TIM_CLK     RCC_APB1Periph_TIM4
#define DCMOTOR_GPIO        GPIOB
#define DCMOTOR_GPIO_CLK    RCC_APB2Periph_GPIOB
#define DCMOTOR_PIN1        GPIO_Pin_8   /* TIM4_CH3 -> IN1 */
#define DCMOTOR_PIN2        GPIO_Pin_9   /* TIM4_CH4 -> IN2 */
#define DCMOTOR_CCR1        TIM_SetCompare3
#define DCMOTOR_CCR2        TIM_SetCompare4
#define DCMOTOR_CH1         TIM_Channel_3
#define DCMOTOR_CH2         TIM_Channel_4
#define DCMOTOR_OC1Init     TIM_OC3Init
#define DCMOTOR_OC2Init     TIM_OC4Init
#define DCMOTOR_OC1Preload  TIM_OC3PreloadConfig
#define DCMOTOR_OC2Preload  TIM_OC4PreloadConfig

#define DCMOTOR_PWM_ARR     100
#define DCMOTOR_PWM_PSC     (72 - 1)

static volatile MotorDirection_t s_dir = MOTOR_DIR_STOP;
static volatile uint8_t s_speed = 0;

static void DCMotor_PinAsGPIO(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = DCMOTOR_PIN1 | DCMOTOR_PIN2;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DCMOTOR_GPIO, &gpio);
}

static void DCMotor_PinAsPWM(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = DCMOTOR_PIN1 | DCMOTOR_PIN2;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DCMOTOR_GPIO, &gpio);
}

static void DCMotor_UpdateOutput(void)
{
    uint16_t pulse = (uint16_t)s_speed; /* 0-100 maps to 0-ARR */
    if (pulse > DCMOTOR_PWM_ARR) pulse = DCMOTOR_PWM_ARR;

    switch (s_dir)
    {
        case MOTOR_DIR_FORWARD:
            DCMotor_PinAsPWM();
            DCMOTOR_CCR1(DCMOTOR_TIM, pulse);  /* IN1 PWM */
            DCMOTOR_CCR2(DCMOTOR_TIM, 0);      /* IN2 0   */
            TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH1, TIM_CCx_Enable);
            TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH2, TIM_CCx_Enable);
            break;

        case MOTOR_DIR_REVERSE:
            DCMotor_PinAsPWM();
            DCMOTOR_CCR1(DCMOTOR_TIM, 0);      /* IN1 0   */
            DCMOTOR_CCR2(DCMOTOR_TIM, pulse);  /* IN2 PWM */
            TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH1, TIM_CCx_Enable);
            TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH2, TIM_CCx_Enable);
            break;

        case MOTOR_DIR_STOP:
        default:
            /* Coast: disable PWM channel, force GPIO low */
            TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH1, TIM_CCx_Disable);
            TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH2, TIM_CCx_Disable);
            DCMotor_PinAsGPIO();
            GPIO_ResetBits(DCMOTOR_GPIO, DCMOTOR_PIN1 | DCMOTOR_PIN2);
            break;
    }
}

void DCMotor_Init(void)
{
    /* Enable clocks */
    RCC_APB1PeriphClockCmd(DCMOTOR_TIM_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(DCMOTOR_GPIO_CLK, ENABLE);

    /* TIM4 base config: 144MHz / 72 / 100 = 20kHz PWM */
    TIM_TimeBaseInitTypeDef tim;
    tim.TIM_Prescaler = DCMOTOR_PWM_PSC;
    tim.TIM_Period = DCMOTOR_PWM_ARR;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(DCMOTOR_TIM, &tim);

    /* TIM4 CH3 & CH4 PWM config */
    TIM_OCInitTypeDef oc;
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_Pulse = 0;
    DCMOTOR_OC1Init(DCMOTOR_TIM, &oc);
    DCMOTOR_OC2Init(DCMOTOR_TIM, &oc);

    DCMOTOR_OC1Preload(DCMOTOR_TIM, TIM_OCPreload_Enable);
    DCMOTOR_OC2Preload(DCMOTOR_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(DCMOTOR_TIM, ENABLE);

    TIM_Cmd(DCMOTOR_TIM, ENABLE);

    s_dir = MOTOR_DIR_STOP;
    s_speed = 0;
    DCMotor_UpdateOutput();
}

void DCMotor_Stop(void)
{
    s_dir = MOTOR_DIR_STOP;
    DCMotor_UpdateOutput();
}

void DCMotor_SetDirection(MotorDirection_t dir)
{
    s_dir = dir;
    DCMotor_UpdateOutput();
}

void DCMotor_SetSpeed(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_speed = percent;
    DCMotor_UpdateOutput();
}

void DCMotor_Run(MotorDirection_t dir, uint8_t speed)
{
    if (speed > 100) speed = 100;
    s_dir = dir;
    s_speed = speed;
    DCMotor_UpdateOutput();
}

void DCMotor_Brake(void)
{
    /* Brake: disable PWM channels, force both pins high */
    TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH1, TIM_CCx_Disable);
    TIM_CCxCmd(DCMOTOR_TIM, DCMOTOR_CH2, TIM_CCx_Disable);
    DCMotor_PinAsGPIO();
    GPIO_SetBits(DCMOTOR_GPIO, DCMOTOR_PIN1 | DCMOTOR_PIN2);
    s_dir = MOTOR_DIR_STOP;
    s_speed = 0;
}
