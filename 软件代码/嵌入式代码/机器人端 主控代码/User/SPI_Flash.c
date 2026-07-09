#include "SPI_Flash.h"
#include "ch32v30x.h"
#include "debug.h"
#include <stdlib.h>

u8 jedec_id[3] = {0xff, 0xff, 0xff};

/**
 * @brief 初始化硬件SPI2 - 用于W25Q512 Flash
 * @note  接线: PB12-CS, PB13-SCK, PB14-MISO, PB15-MOSI
 *        SPI模式: Mode 0 (CPOL=0, CPHA=0) 
 *        时钟: 36MHz (HCLK=144MHz时，144/4=36MHz)
 */
void SPI2_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef SPI_InitStructure;
    
    // 1. 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);   // GPIOB时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);    // SPI2时钟
    
    // 2. 配置GPIO
    // PB13-SCK, PB15-MOSI: 复用推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // PB14-MISO: 浮空输入（或上拉输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // PB12-CS: 通用推挽输出（软件控制片选）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // CS默认高电平（未选中）
    GPIO_SetBits(GPIOB, GPIO_Pin_12);
    
    // 3. 配置SPI2
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;  // 全双工
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;                       // 主机模式
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;                   // 8位数据
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;                          // 时钟极性低 (Mode 0)
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;                        // 第一个边沿采样
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;                           // 软件NSS控制
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;  // 分频: 144/4=36MHz
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;                  // MSB先行
    SPI_InitStructure.SPI_CRCPolynomial = 7;                            // CRC多项式（不用可随便设）
    
    SPI_Init(SPI2, &SPI_InitStructure);
    
    // 4. 使能SPI2
    SPI_Cmd(SPI2, ENABLE);

    // 5. 进入4字节地址模式 - W25Q512JV数据手册 Page 12, 6.1.4 3-Byte / 4-Byte Address Modes
    W25Q512_CS_Ctrl(0);
    SPI2_ReadWriteByte(0xB7);  // Enter 4-Byte Address Mode
    W25Q512_CS_Ctrl(1);

    printf("[SPI FLASH]>>> Init SUCCESS!\r\n");
}

/**
 * @brief SPI2发送一个字节并接收返回数据
 * @param byte 要发送的数据
 * @return 接收到的数据
 */
uint8_t SPI2_ReadWriteByte(uint8_t byte)
{
    // 等待发送缓冲区空
    while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
    
    // 发送数据
    SPI_I2S_SendData(SPI2, byte);
    
    // 等待接收缓冲区非空
    while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET);
    
    // 读取接收数据
    return SPI_I2S_ReceiveData(SPI2);
}

/**
 * @brief 片选控制函数
 * @param level 0-选中(拉低), 1-释放(拉高)
 */
void W25Q512_CS_Ctrl(uint8_t level)
{
    if(level == 0)
        GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    else
        GPIO_SetBits(GPIOB, GPIO_Pin_12);
}

/**
 * 将字节数组转换为十六进制字符串，格式如 "0xEF 0x40 0x20"
 * param data 字节数组
 * param len  数组长度
 * return     动态分配的字符串（调用者需 free）
 */
/*
如果只是printf，不需要下面这段代码，直接%X
static char* bytes_to_hex_string(const unsigned char* data, size_t len) {
    // 每个字节最多占 5 个字符："0xEF" + 空格（最后一个字节无空格）
    // 总长度 = len * 5 + 1（结尾空字符）
    char* result = malloc(len * 5 + 1);
    if (!result) return NULL;

    char* p = result;
    for (size_t i = 0; i < len; i++) {
        if (i > 0) {
            *p++ = ' ';          // 字节间加空格
        }
        p += sprintf(p, "0x%02X", data[i]);  // 格式化为 "0xEF"
    }
    *p = '\0';  // 终止字符串
    return result;
}
*/

/**
 * @brief 读取W25Q512的JEDEC ID
 * @param NO.      jedec_id 指向3字节数组的指针，存储Manufacturer ID和Device ID
 * @note  时序: CS拉低 -> 发送0x9F -> 接收3字节 -> CS拉高
 *        W25Q512正常返回: 0xEF (Winbond), 0x40 (Memory Type), 0x20 (Capacity: 512Mbit=64MB)
 */
void W25Q512_ReadJEDEC_ID(void)
{
    uint8_t i;
    // 片选选中
    W25Q512_CS_Ctrl(0);
    
    // 发送Read JEDEC ID指令 0x9F
    SPI2_ReadWriteByte(0x9F);
    
    // 接收3字节: Manufacturer ID, Memory Type, Capacity
    for(i = 0; i < 3; i++) {
        jedec_id[i] = SPI2_ReadWriteByte(0xFF);  // 发送dummy字节读取数据
    }
    
    // 片选释放
    W25Q512_CS_Ctrl(1);
    printf("[SPI FLASH]>>> Chip jedec_id is %X %X %X\r\n", jedec_id[0], jedec_id[1], jedec_id[2]);
    if(jedec_id[0]==0xEF && jedec_id[1]==0x40 && jedec_id[2]==0x20)
    printf("[SPI FLASH]>>> Chip is W25Q512 (64MegaBytes)\r\n");
}

/**
 * @brief 验证W25Q512真伪：擦最后一页 -> 写0x00-0x07 -> 读回校验
 * @return 0-真芯片(测试通过), 1-假芯片或损坏, 2-擦除失败, 3-写入失败, 4-数据校验失败
 * @note  操作地址：0x03FFFF00 (最后一页起始) + 0xF8偏移 = 0x03FFFFF8 (最后8字节)
 */
uint8_t W25Q512_VerifyChip(void)
{
    uint8_t write_data[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    uint8_t read_data[8] = {0};
    uint32_t page_addr = 0x03FFFF00;      // 最后一页起始地址
    uint32_t target_addr = 0x03FFFFF8;    // 最后8字节起始地址 (0x03FFFF00 + 0xF8)
    uint8_t status;
    uint8_t i;
    
    printf("=== W25Q512 Chip Verification ===\r\n");
    printf("Target Address: 0x%08X\r\n", target_addr);
    
    // ========== 步骤1：擦除最后一页所在的扇区 ==========
    printf("1. Erasing sector (4KB) at 0x%08X ... ", page_addr);
    
    // 写使能
    W25Q512_CS_Ctrl(0);
    SPI2_ReadWriteByte(0x06);  // Write Enable
    W25Q512_CS_Ctrl(1);
    
    // 扇区擦除 4KB (0x21 = Sector Erase 4-byte)
    W25Q512_CS_Ctrl(0);
    SPI2_ReadWriteByte(0x21);              // Sector Erase 4-byte instruction
    SPI2_ReadWriteByte((page_addr >> 24) & 0xFF);  // Address[31:24]
    SPI2_ReadWriteByte((page_addr >> 16) & 0xFF);  // Address[23:16]
    SPI2_ReadWriteByte((page_addr >> 8)  & 0xFF);  // Address[15:8]
    SPI2_ReadWriteByte(page_addr & 0xFF);          // Address[7:0]
    W25Q512_CS_Ctrl(1);
    
    // 等待擦除完成（轮询BUSY位）
    Delay_Ms(1);  // 先等1ms
    do {
        W25Q512_CS_Ctrl(0);
        SPI2_ReadWriteByte(0x05);          // Read Status Register-1
        status = SPI2_ReadWriteByte(0xFF);
        W25Q512_CS_Ctrl(1);
        if(status & 0x01) Delay_Ms(5);     // BUSY=1，继续等
    } while(status & 0x01);
    
    // 验证擦除结果（读该页前8字节，应为0xFF）
    W25Q512_CS_Ctrl(0);
    SPI2_ReadWriteByte(0x13);              // Read Data 4-byte
    SPI2_ReadWriteByte((page_addr >> 24) & 0xFF);
    SPI2_ReadWriteByte((page_addr >> 16) & 0xFF);
    SPI2_ReadWriteByte((page_addr >> 8)  & 0xFF);
    SPI2_ReadWriteByte(page_addr & 0xFF);
    status = SPI2_ReadWriteByte(0xFF);     // 读第一个字节
    W25Q512_CS_Ctrl(1);
    
    if(status != 0xFF) {
        printf("FAILED! Data=0x%02X (should be 0xFF)\r\n", status);
        return 2;  // 擦除失败
    }
    printf("OK\r\n");
    
    // ========== 步骤2：写入0x00-0x07到最后一页最后8字节 ==========
    printf("2. Writing 0x00-0x07 to 0x%08X ... ", target_addr);
    
    // 写使能
    W25Q512_CS_Ctrl(0);
    SPI2_ReadWriteByte(0x06);
    W25Q512_CS_Ctrl(1);
    
    // 页编程 (0x12 = Page Program 4-byte)
    W25Q512_CS_Ctrl(0);
    SPI2_ReadWriteByte(0x12);              // Page Program 4-byte
    SPI2_ReadWriteByte((target_addr >> 24) & 0xFF);
    SPI2_ReadWriteByte((target_addr >> 16) & 0xFF);
    SPI2_ReadWriteByte((target_addr >> 8)  & 0xFF);
    SPI2_ReadWriteByte(target_addr & 0xFF);
    
    // 写入8字节数据
    for(i = 0; i < 8; i++) {
        SPI2_ReadWriteByte(write_data[i]);
    }
    W25Q512_CS_Ctrl(1);
    
    // 等待编程完成
    Delay_Ms(1);
    do {
        W25Q512_CS_Ctrl(0);
        SPI2_ReadWriteByte(0x05);
        status = SPI2_ReadWriteByte(0xFF);
        W25Q512_CS_Ctrl(1);
        if(status & 0x01) Delay_Ms(1);
    } while(status & 0x01);
    
    printf("OK\r\n");
    
    // ========== 步骤3：读取并校验 ==========
    printf("3. Reading back and verifying ... ");
    
    W25Q512_CS_Ctrl(0);
    SPI2_ReadWriteByte(0x13);              // Read Data 4-byte
    SPI2_ReadWriteByte((target_addr >> 24) & 0xFF);
    SPI2_ReadWriteByte((target_addr >> 16) & 0xFF);
    SPI2_ReadWriteByte((target_addr >> 8)  & 0xFF);
    SPI2_ReadWriteByte(target_addr & 0xFF);
    
    // 读8字节
    for(i = 0; i < 8; i++) {
        read_data[i] = SPI2_ReadWriteByte(0xFF);
    }
    W25Q512_CS_Ctrl(1);
    
    // 校验
    for(i = 0; i < 8; i++) {
        if(read_data[i] != write_data[i]) {
            printf("FAILED!\r\n");
            printf("   Write: ");
            for(uint8_t j = 0; j < 8; j++) printf("%02X ", write_data[j]);
            printf("\r\n   Read:  ");
            for(uint8_t j = 0; j < 8; j++) printf("%02X ", read_data[j]);
            printf("\r\n   Diff @ byte %d: wrote 0x%02X, read 0x%02X\r\n", 
                   i, write_data[i], read_data[i]);
            return 4;  // 数据校验失败
        }
    }
    
    printf("OK\r\n");
    printf("   Data: ");
    for(i = 0; i < 8; i++) printf("%02X ", read_data[i]);
    printf("\r\n");
    printf("=== Verification PASSED! Chip appears genuine. ===\r\n");
    
    return 0;  // 测试通过
}
