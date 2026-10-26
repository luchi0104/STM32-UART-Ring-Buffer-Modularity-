# STM32 UART Interrupt 與 Ring Buffer — 除錯紀錄

## 1. 專案環境

| 項目 | 設定 |
|---|---|
| 開發板／MCU | NUCLEO-L476RG／STM32L476RG（Arm Cortex-M4） |
| IDE | STM32CubeIDE 2.2.0 |
| 通訊 | USART2，板載 ST-LINK Virtual COM Port |
| 終端機 | Tera Term（測試時 COM6） |
| 串列設定 | 115200 baud、8N1 |
| 接收架構 | HAL 中斷式單 Byte 接收 + Ring Buffer |
| Ring Buffer | 64 格，有效容量 63 Bytes |

本文記錄實際問題、排查方法、修正與測試證據。結果僅代表當時韌體版本及測試條件，不等於完整可靠性驗證。

## 2. 初期 UART 問題

### 2.1 Tera Term 亂碼

**現象：** STM32 有傳送資料，但終端機顯示亂碼。

**原因：** Tera Term 設定 **9600 baud**，USART2 設定 **115200 baud**。

**修正：** 將終端機調整成 115200、8N1，成功顯示 `Hello STM32!`。

8N1 每 Byte 含 Start 1 Bit、Data 8 Bits、Stop 1 Bit，總共 10 Bits；115200 baud 下每 Byte 約 **86.8 µs**。

### 2.2 看不到輸入／指令無反應

**現象：** 輸入字元時看不到內容、按 Enter 無反應，或開啟 Local Echo 後才看見文字。

**排查：** 區分 Tera Term 的 **Local Echo** 與 STM32 真正回傳的 **UART Echo**；檢查 Enter 是 `\r`、`\n` 還是 `\r\n`，並確認命令解析與 TX 回覆。測試 MCU Echo 時應關閉 Local Echo，以免把本機顯示誤認為 UART 回傳。

**結果：** 調整設定與程式後，UART Echo 與 LED 指令控制成功運作。

## 3. Interrupt 接收除錯

### 3.1 UART 能傳送，卻無法回傳接收資料

排查順序：

1. 確認初始化後呼叫 `HAL_UART_Receive_IT(&huart2, &rx_data, 1)`，並檢查回傳值。
2. 確認 NVIC 已啟用 USART2 IRQ，`stm32l4xx_it.c` 的 `USART2_IRQHandler()` 有呼叫 `HAL_UART_IRQHandler()`。
3. 觀察 `huart2.RxState`、`huart2.RxXferCount`、`huart2.ErrorCode`。
4. 在 `USART2_IRQHandler()` 與 `HAL_UART_RxCpltCallback()` 設 Breakpoint，區分中斷入口與應用程式處理問題。

**當時數值：** `RxState=34 (0x22, BUSY_RX)`、`RxXferCount=1`、`ErrorCode=0`。這表示 HAL 已啟動等待 1 Byte 的接收，**不能單靠這些數值證明資料已到達或 Callback 已執行**。

**觀念：** `HAL_UART_Receive_IT()` 是啟動中斷式接收；`&rx_data` 指定資料要存放的位址，HAL 不依賴變數名稱。

## 4. Ring Buffer 與軟體 Overflow

UART RX Callback 為**單一 Producer**，`main()` 為**單一 Consumer**。`head` 是下一個寫入索引，`tail` 是下一個讀取索引。保留一格來區分滿與空。

```c
next_head = (head + 1U) % BUFFER_SIZE;
if (next_head == tail) {
    overflow_count++;
    return 0; // 滿了：丟棄最新資料
}
rx_buffer[head] = data;
head = next_head;
```

計算待處理資料量：

```c
used = (head + BUFFER_SIZE - tail) % BUFFER_SIZE;
```

### 4.1 Overflow=17

**測試：** 暫停或刻意讓 Consumer 跟不上輸入。

**結果：** 64 格 Buffer 有效容量為 63 Bytes；一次測試得到 `Overflow=17`，符合起始為空、無 Pop、嘗試 Push 80 Bytes 時的 `80 - 63 = 17`。

**區別：** 軟體 Ring Buffer Overflow 與 UART 硬體 ORE 是兩種不同問題；ISR 正常取走硬體資料時，軟體 Buffer 仍可能滿。

### 4.2 Wrap-around 與長字串

長字串測試曾顯示 `Head == Tail`、`Used=0`、`Overflow=0` 且可 Echo。這支持基本功能正常，但**最終索引快照不能嚴格證明每個 Byte 都完整收到，或確實捕捉到 `63 → 0` 的瞬間**。更完整的測試應比對完整收發內容與繞回次數。

## 5. UART ORE 故障注入

### 5.1 測試方法

ORE（Overrun Error）可能在前一筆資料尚未取走時，下一筆資料就抵達而發生。測試韌體暫時停用 USART2 的 NVIC 中斷，但不關閉 UART 硬體：

```c
HAL_NVIC_DisableIRQ(USART2_IRQn);
// 測試期間從 PC 連續傳送資料
// 約 5 秒後：
HAL_NVIC_EnableIRQ(USART2_IRQn);
```

**重點：** 停用 NVIC IRQ 不等於停用 USART。硬體仍可接收並設定錯誤旗標；故障注入期間可能遺失資料。

### 5.2 硬體層直接驗證

在 `USART2_IRQHandler()` 內、呼叫 `HAL_UART_IRQHandler(&huart2)` **之前**設 Breakpoint，後續改用條件式 Breakpoint 避免正常接收時就停住。

```text
USART2->ISR                = 6291704 (0x006000B8)
USART2->ISR & USART_ISR_ORE = 8
USART2->CR1                = 45 (0x0000002D)
huart2.ErrorCode           = 0
huart2.RxState             = 34 (BUSY_RX)
huart2.RxXferCount         = 1
```

ORE 位於 ISR 的 Bit 3（`1 << 3 = 8`）。AND 結果為 **8**，直接證明 **HAL 處理前硬體 ORE 旗標已設定**。此時 HAL 的 `ErrorCode=0` 不矛盾，因為 HAL 尚未處理這次中斷。Call Stack 顯示重新啟用 NVIC 附近進入 IRQ，符合待處理中斷被執行的情況。

## 6. 關鍵 Bug：硬體有 ORE，Callback 卻讀到 ErrorCode=0

### 6.1 異常現象

```text
UART Errors=1 ORE=0 FE=0 NE=0 PE=0
Recovery=0 RestartFail=0 LastError=0x00000000
```

`UART Errors` 增加，卻未正確分類 ORE，與 Debugger 的硬體證據不一致。

### 6.2 HAL 原始碼追查

舊設計在接收完成 Callback 中立即重啟接收：

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        RingBuffer_Push(rx_data);
        HAL_UART_Receive_IT(&huart2, &rx_data, 1);
    }
}
```

檢查專案的 STM32L4 HAL 原始碼，發現以下**有程式碼依據的可能執行順序**：

1. `HAL_UART_IRQHandler()` 偵測 ORE，更新 `huart->ErrorCode`。
2. 若有待處理的接收資料，HAL 可能先進入接收 ISR 路徑。
3. 應用程式的 RxCpltCallback 立即呼叫 `HAL_UART_Receive_IT()`。
4. 這個 HAL 實作在啟動新接收時，會將 `ErrorCode` 重設為 `HAL_UART_ERROR_NONE`。
5. 後續 ErrorCallback 可能因此讀到 0。

此路徑能解釋異常，且修正後的測試結果支持這個分析；但沒有保存失敗當下每一行的完整逐步追蹤，因此不宣稱已排除所有其他可能原因。

### 6.3 修正：延後到 main 重啟接收

接收完成 Callback 只 Push 資料並設旗標：

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        RingBuffer_Push(&uart_rx_rb, rx_data);
        uart_rx_restart_pending = 1;
    }
}
```

錯誤 Callback 先保存錯誤碼及暫存器快照：

```c
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        uint32_t error = HAL_UART_GetError(huart);
        uart_last_error = error;
        uart_last_isr = huart->Instance->ISR;
        uart_last_cr1 = huart->Instance->CR1;
        uart_error_count++;

        if (error & HAL_UART_ERROR_ORE) {
            uart_ore_count++;
            uart_recovery_pending = 1;
            uart_rx_restart_pending = 1;
        }
        if (error & HAL_UART_ERROR_FE) uart_fe_count++;
        if (error & HAL_UART_ERROR_NE) uart_ne_count++;
        if (error & HAL_UART_ERROR_PE) uart_pe_count++;
    }
}
```

由 `main()` 統一重啟接收；當錯誤觸發的重啟回傳 `HAL_OK` 時，才增加 `Recovery`。這避免在同一次 HAL 錯誤處理尚未結束前，因立即重啟接收而清除 ErrorCode。

**取捨：** 每次 1 Byte 接收完成後，要等待 main 再啟動下一筆接收，存在接收空窗。這是教學／除錯架構，**尚未證明適合高速持續傳輸**。

## 7. 最終回歸測試

修正後成功記錄：

```text
UART Errors=1 ORE=1 FE=0 NE=0 PE=0
Recovery=1 RestartFail=0 LastError=0x00000008
```

Ring Buffer 拆分成 `ring_buffer.h` 與 `ring_buffer.c` 後，再次測試：

```text
Head=12 Tail=12 Used=0 Overflow=0
UART Errors=1 ORE=1 FE=0 NE=0 PE=0
Recovery=1 RestartFail=0 LastError=0x00000008
ISR=0x006010D0 CR1=0x0000000D
```

故障後仍觀察到 `ABC123` Echo，支持**本次單次 ORE 情境下成功偵測錯誤並恢復接收**。印出的 `ISR`、`CR1` 是 ErrorCallback 當下的快照，並非恢復後的即時暫存器狀態。模組化後也重新驗證了基本 Echo 與錯誤恢復。

## 8. 其他編譯與 Debug 問題

### 8.1 重複宣告 `error`

編譯器回報 `redefinition of 'error'`。原因是同一作用域重複寫入 `uint32_t error = HAL_UART_GetError(huart);`；刪除重複宣告即可。

### 8.2 `snprintf()` 格式與參數不匹配

曾出現 `%u` 收到 `char *`、多餘參數等 Warning。原因是把新格式字串放在**逗號後**，使它成為另一個函式參數，而不是原格式字串的一部分。修正方式是合併相鄰字串常值，並逐一核對 `%u`、`%lu`、`%08lX` 與實際參數型別、順序。當時曾印出不可能的 Head 數值，因此該筆數據不列為有效測試結果。

### 8.3 Breakpoint 會影響即時性

CPU 停在 ISR 時，外部 UART 資料仍可能到達。應使用條件式 Breakpoint，明確記錄停止位置，避免把 Debug 暫停期間的行為直接當成正常連續執行結果。

## 9. 已知限制與改善方向

- **RX 重啟空窗：** main 逐 Byte 重啟接收，可能在高流量下漏資料；未來可研究 UART DMA + Circular Buffer，或重新設計 ISR 重啟策略。
- **Recovery 覆蓋範圍有限：** 單次 ORE 成功不代表多次 ORE、同時多種錯誤、長時間壓力或斷線重連都已驗證。
- **Ring Buffer 並行存取：** 目前假設單一 ISR Producer、單一 main Consumer。`volatile` 不是通用的同步原語；跨平台使用前需檢查原子性與記憶體順序。
- **Overflow 策略：** Buffer 滿時丟棄最新 Byte，這是刻意採用的政策。
- **Blocking TX：** `HAL_UART_Transmit()` 可能延遲 main 的處理；後續可改為非阻塞傳送。
- **Clear 同步：** ISR 可能 Push 時，main 不能任意重設 Head/Tail，須先停止或同步 Producer。
- **測試嚴謹度：** 可增加完整收發資料比對、Wrap Counter、高負載丟包率與重複故障注入測試。

## 10. 建議的測試截圖

將圖片放在 `docs/images/`，並依實際上傳檔名調整：

| 建議檔名 | 證明內容 |
|---|---|
| `ring_buffer_overflow.png` | 控制條件下的 `Overflow=17` |
| `uart_ore_debugger.png` | HAL 處理前 `USART2->ISR & USART_ISR_ORE = 8` |
| `uart_ore_recovery.png` | `ORE=1`、`Recovery=1`、`LastError=0x00000008` |
| `uart_regression_test.png` | 模組化後 Echo、無軟體 Overflow、ORE 恢復測試 |

## 11. 學習收穫

這個專案從 UART Echo 進一步延伸到中斷狀態分析、Ring Buffer、軟體 Overflow 與硬體 ORE 的區分、HAL 處理前的暫存器觀察、HAL 原始碼追查，以及錯誤恢復測試。它適合作為**韌體開發與除錯能力的學習展示**，同時清楚揭露吞吐量及可靠性尚未驗證的限制。
