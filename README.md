# STM32-UART-Ring-Buffer-Modularity-
STM32 UART communication with interrupt-driven RX, ring buffer management, overflow detection, and ORE fault injection and recovery testing.

# STM32 UART Interrupt & Ring Buffer

基於 **STM32 NUCLEO-L476RG** 開發板，使用 STM32 HAL Library 實作 UART 中斷式接收、模組化 Ring Buffer、資料溢位偵測，以及 UART 硬體錯誤處理與恢復機制。

本專案從基本 UART Blocking TX/RX 開始，逐步改進為 Interrupt-driven UART，並透過實際硬體測試與 STM32CubeIDE Debugger，分析 UART Overrun Error（ORE）的發生原因及 HAL Error Handling 流程。

## 1. 開發環境（Development Environment）

| 項目 | 規格 |
|------|------|
| Development Board | NUCLEO-L476RG |
| MCU | STM32L476RG |
| CPU | ARM Cortex-M4 |
| IDE | STM32CubeIDE 2.2.0 |
| Configuration Tool | STM32CubeMX |
| Firmware Library | STM32CubeL4 HAL |
| Debugger | ST-LINK |
| Serial Terminal | Tera Term |
| UART Peripheral | USART2 |
| Baud Rate | 115200 |
| UART Format | 8N1 |
| Ring Buffer Size | 64 Bytes |
| Effective Capacity | 63 Bytes |

USART2 透過開發板的 ST-LINK Virtual COM Port 與電腦通訊。

## 2. 專案功能（Features）

### UART Communication

- UART Blocking TX/RX 基礎通訊測試
- UART Interrupt-driven Reception
- UART Echo（接收資料後回傳）
- UART 接收完成 Callback
- UART 硬體錯誤 Callback

### Ring Buffer

- Circular Buffer 資料結構
- FIFO（First In, First Out）
- RingBuffer Push / Pop
- Head / Tail Index Management
- Wrap-around 處理
- Buffer Overflow Detection
- Buffer Usage Statistics
- 模組化設計（ring_buffer.c / ring_buffer.h）

### UART Error Handling

- Overrun Error（ORE）偵測
- Framing Error（FE）計數
- Noise Error（NE）計數
- Parity Error（PE）計數
- UART ErrorCode 紀錄
- UART Receive Restart / Recovery
- 接收重啟失敗次數統計

### Debugging & Testing

- STM32CubeIDE Breakpoint
- Conditional Breakpoint
- Call Stack Analysis
- Expressions / Register Inspection
- HAL Source Code Debugging
- UART ORE Fault Injection
- UART Recovery Verification

> 本專案已實際驗證 ORE 故障注入與單次接收恢復。FE、NE、PE 已加入統計邏輯，但尚未分別進行故障注入測試。

## 3. 系統架構（System Architecture）

UART 接收資料後，透過中斷通知 CPU，再由 HAL Callback 將資料存入 Ring Buffer。

主程式負責讀取 Ring Buffer，並進行 UART Echo 或指令處理。

```text
                PC / Tera Term
                      |
                      | UART 115200 8N1
                      |
                 USART2 RX
                      |
                      v
              USART2_IRQHandler()
                      |
                      v
              HAL_UART_IRQHandler()
                      |
                      v
           HAL_UART_RxCpltCallback()
                      |
                      | RingBuffer_Push()
                      v
              +----------------+
              |   Ring Buffer  |
              |                |
              |  Head / Tail   |
              |  FIFO Storage  |
              +----------------+
                      |
                      | RingBuffer_Pop()
                      v
                  main()
                      |
                      v
              UART Echo / Command
                      |
                      v
                 USART2 TX
                      |
                      v
                PC / Tera Term
```

目前採用 Single Producer Single Consumer（SPSC）架構：

- Producer：UART Receive Complete Callback
- Consumer：main loop

Ring Buffer 與 STM32 HAL 分離，可以重複使用於其他資料接收情境。

## 4. 專案結構（Project Structure）

```text
STM32-UART-Interrupt-RingBuffer/
|
+-- Core/
|   +-- Inc/
|   |   +-- main.h
|   |   +-- ring_buffer.h
|   |   +-- stm32l4xx_it.h
|   |
|   +-- Src/
|   |   +-- main.c
|   |   +-- ring_buffer.c
|   |   +-- stm32l4xx_it.c
|   |   +-- stm32l4xx_hal_msp.c
|   |
|   +-- Startup/
|
+-- Drivers/
|   +-- CMSIS/
|   +-- STM32L4xx_HAL_Driver/
|
+-- docs/
|   +-- images/
|   |   +-- ring_buffer_overflow.png
|   |   +-- uart_ore_recovery.png
|   |   +-- uart_regression_test.png
|   |   +-- uart_ore_debugger.png
|   |
|   +-- debugging_notes.md
|
+-- STM32_UART_Lab01.ioc
+-- README.md
+-- .gitignore
```

其餘 STM32CubeIDE 專案設定、Linker Script 與 HAL 支援檔案依實際專案保留。

## 5. Ring Buffer 設計

### 5.1 Circular Buffer

Ring Buffer 使用固定大小陣列儲存 UART 接收到的資料。

```c
#define RING_BUFFER_SIZE 64U

typedef struct
{
    uint8_t buffer[RING_BUFFER_SIZE];

    volatile uint16_t head;
    volatile uint16_t tail;

    volatile uint32_t overflow_count;

} RingBuffer_t;
```

其中：

- `head`：下一個寫入位置
- `tail`：下一個讀取位置
- `overflow_count`：Buffer 已滿時的寫入失敗次數

採用保留一格的方式區分 Buffer Full 與 Buffer Empty。

因此有效容量為：

```text
Effective Capacity = BUFFER_SIZE - 1
                   = 64 - 1
                   = 63 Bytes
```

### 5.2 Push Operation

```c
int RingBuffer_Push(RingBuffer_t *rb, uint8_t data)
{
    if (rb == NULL)
        return 0;

    uint16_t next_head =
        (rb->head + 1U) % RING_BUFFER_SIZE;

    if (next_head == rb->tail)
    {
        rb->overflow_count++;
        return 0;
    }

    rb->buffer[rb->head] = data;
    rb->head = next_head;

    return 1;
}
```

當下一個 Head 等於 Tail，代表 Buffer 已滿。

此時不覆蓋舊資料，而是增加 Overflow Counter。

### 5.3 Pop Operation

```c
int RingBuffer_Pop(RingBuffer_t *rb, uint8_t *data)
{
    if (rb == NULL || data == NULL)
        return 0;

    if (rb->head == rb->tail)
        return 0;

    *data = rb->buffer[rb->tail];

    rb->tail =
        (rb->tail + 1U) % RING_BUFFER_SIZE;

    return 1;
}
```

當 Head 等於 Tail，代表 Buffer 為空。

### 5.4 Buffer Usage

```c
uint16_t used =
    (head + RING_BUFFER_SIZE - tail)
    % RING_BUFFER_SIZE;
```

透過 Modulo 運算處理 Head / Tail 繞回問題。

## 6. UART Interrupt Reception

使用 HAL UART Interrupt API 啟動接收：

```c
HAL_UART_Receive_IT(&huart2, &rx_data, 1);
```

接收完成後，由 Callback 將資料存入 Ring Buffer。

```c
void HAL_UART_RxCpltCallback(
    UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        RingBuffer_Push(&uart_rx_rb, rx_data);

        uart_rx_restart_pending = 1;
    }
}
```

目前版本由 main loop 負責重新啟動下一次接收。

這項設計是為了避免在 HAL 尚未完成錯誤處理前，重新呼叫接收 API 而清除 ErrorCode。

但這也會產生接收空窗，因此不適合作為高吞吐量 UART 接收的最終架構。

## 7. 測試結果（Test Results）

### 7.1 UART Echo Test

透過 Tera Term 傳送：

```text
ABC123
```

STM32 成功回傳相同字元。

測試結果：

```text
Head=7 Tail=7 Used=0 Overflow=0
UART Errors=0 ORE=0 FE=0 NE=0 PE=0
```

表示正常測試下，Ring Buffer 已被讀空，且沒有記錄到溢位或 UART 硬體錯誤。

### 7.2 Ring Buffer Overflow Test

Ring Buffer Size = 64 Bytes。

有效容量 = 63 Bytes。

在測試中暫停 Consumer，並連續送入 80 Bytes。

```text
Input Data       = 80 Bytes
Effective Buffer = 63 Bytes
Overflow         = 17 Bytes
```

測試結果：

```text
Head=0 Tail=0 Used=0 Overflow=17
```

此結果驗證了軟體 Buffer Full 判斷與 Overflow Counter。

![Ring Buffer Overflow Test](docs/images/ring_buffer_overflow.png)

### 7.3 UART Hardware ORE Fault Injection

為了驗證 UART 硬體 Overrun Error，測試時暫時停用 USART2 NVIC Interrupt。

```c
HAL_NVIC_DisableIRQ(USART2_IRQn);
```

在中斷停用期間，透過 Tera Term 傳送連續資料。

USART2 硬體持續接收，但 CPU 無法及時處理接收事件，因此產生 ORE。

重新啟用中斷：

```c
HAL_NVIC_EnableIRQ(USART2_IRQn);
```

透過 STM32CubeIDE Debugger，在 HAL 處理中斷之前讀取硬體暫存器：

```text
USART2->ISR = 0x006000B8

USART2->ISR & USART_ISR_ORE = 8
```

由於 ORE 位於 ISR Register Bit 3：

```text
1 << 3 = 8
```

因此確認 UART 硬體確實產生 Overrun Error。

![UART ORE Debugger](docs/images/uart_ore_debugger.png)

### 7.4 UART ORE Recovery Test

最終測試結果：

```text
UART Errors=1 ORE=1 FE=0 NE=0 PE=0
Recovery=1 RestartFail=0
LastError=0x00000008
```

其中：

- `UART Errors=1`：ErrorCallback 執行並記錄錯誤
- `ORE=1`：成功辨識 Overrun Error
- `LastError=0x00000008`：對應 HAL_UART_ERROR_ORE
- `Recovery=1`：成功重新啟動 UART 接收
- `RestartFail=0`：沒有記錄到接收重啟失敗

恢復後再次輸入資料，UART Echo 仍可正常運作。

此結果證明本次 ORE 故障注入後，接收功能成功恢復。

![UART ORE Recovery](docs/images/uart_ore_recovery.png)

### 7.5 Regression Test After Modularization

將 Ring Buffer 拆分成獨立 `.c/.h` 模組後，重新測試：

- UART Echo
- Long-input Reception
- Ring Buffer Push / Pop
- Buffer Usage Statistics
- ORE Fault Injection
- UART Recovery

測試結果顯示正常 Echo 與單次 ORE Recovery 仍可運作。

![UART Regression Test](docs/images/uart_regression_test.png)

## 8. 重要除錯經驗（Debugging Experience）

### 8.1 UART Baud Rate Mismatch

**問題：** Tera Term 出現亂碼。

**原因：** STM32 設定 115200 baud，但 Tera Term 設定 9600 baud。

**解決：** 統一兩端 Baud Rate。

### 8.2 UART Interrupt 無法正常 Echo

**問題：** UART 可以傳送歡迎訊息，但接收後沒有正常回傳。

**排查方式：**

- 檢查 USART2 NVIC
- 檢查 HAL_UART_Receive_IT() 回傳值
- 觀察 huart2.RxState
- 使用 Breakpoint 追蹤 USART2_IRQHandler()
- 檢查 HAL_UART_RxCpltCallback()

透過 Debugger 與 HAL 狀態觀察，逐步確認接收流程。

### 8.3 ORE 發生但 ErrorCode 為 0

**問題：**

```text
UART Errors=1
ORE=0
LastError=0x00000000
```

但硬體 Debugger 已確認 ORE 旗標存在。

**原因分析：**

在原本的接收流程中，RxCpltCallback 會立即重新啟動 UART 接收。

在本專案使用的 HAL 執行路徑中，可能先處理 RXNE 資料並呼叫 RxCpltCallback，再完成錯誤處理。

重新呼叫 HAL_UART_Receive_IT() 可能清除 Handle 的 ErrorCode，使後續 ErrorCallback 讀到 0。

**解決方法：**

將接收重啟動作移至 main loop，由 Callback 設定 Pending Flag。

修改後成功觀察到：

```text
ORE=1
Recovery=1
LastError=0x00000008
```

這是本專案最重要的 HAL Source Code Debugging 經驗。

更完整的排查過程請參閱：

[Debugging Notes](docs/debugging_notes.md)

## 9. 已知限制（Known Limitations）

### 9.1 接收重啟空窗

目前每次接收完成後，由 main loop 重新呼叫 HAL_UART_Receive_IT()。

因此接收完成到下一次啟動之間存在時間空窗。

在高速連續資料傳輸時，仍可能發生資料遺失或 ORE。

### 9.2 Blocking UART Transmission

目前 UART Echo 使用：

```c
HAL_UART_Transmit(&huart2, &data, 1, 100);
```

這是 Blocking Transmission。

當資料量增加時，主程式可能因傳送操作而延遲處理其他工作。

### 9.3 Ring Buffer Concurrency

目前 Ring Buffer 設計主要針對單核心 MCU 的 SPSC 使用情境。

`volatile` 並不等於完整的 Thread Safety。

若未來使用 RTOS、多個 Producer 或多個 Consumer，需要重新評估同步與記憶體順序。

### 9.4 Error Recovery Coverage

目前已驗證單次 ORE Fault Injection 與接收恢復。

尚未完整驗證：

- 多次連續 ORE
- 長時間高負載
- FE / NE / PE 故障注入
- 長期 BUSY_RX 異常狀態
- 接收資料完整性與吞吐量

因此本專案定位為 UART Interrupt、Ring Buffer 與 Error Handling 的實作及除錯練習，而非完整的產品級 UART Driver。

## 10. 如何執行（Getting Started）

1. Clone 本 Repository。
2. 使用 STM32CubeIDE 2.2.0 匯入現有專案。
3. 確認 STM32CubeL4 HAL 與 CMSIS 相關檔案可正常編譯。
4. 將 NUCLEO-L476RG 透過 ST-LINK USB 連接電腦。
5. Build 並燒錄程式。
6. 開啟 Tera Term，選擇 ST-LINK Virtual COM Port。
7. 設定 Serial Port 為 115200 baud、8N1、無 Flow Control。
8. 關閉 Local Echo，輸入 `ABC123` 測試 UART Echo。
9. 輸入 `?` 查看 Ring Buffer 與 UART Error Statistics。

ORE 故障注入使用的 `!` 指令僅適用於保留測試功能的版本，不建議在正式應用中啟用。

## 11. 未來改善方向（Future Improvements）

- UART DMA Reception
- DMA Circular Buffer
- UART IDLE Line Detection
- Non-blocking UART TX
- 更完整的 UART Error Recovery State Machine
- Ring Buffer Unit Testing
- 高負載與資料完整性測試
- I2C / SPI Peripheral Communication

## 12. 專案學習成果（Key Takeaways）

透過本專案，實際練習：

- STM32CubeMX 周邊設定
- STM32CubeIDE Build / Flash / Debug
- UART Blocking 與 Interrupt 的差異
- HAL_UART_Receive_IT() 與 Callback
- NVIC 與 UART ISR 執行流程
- Ring Buffer 與 FIFO 設計
- Pointer、Struct、Volatile 與 Bitwise Operation
- ISR / Main Loop 資料交換
- HAL ErrorCode 與硬體暫存器分析
- UART ORE Fault Injection
- UART Receive Recovery
- C 程
