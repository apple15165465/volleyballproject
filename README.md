# Spike Lab：M5Stack 排球揮臂數據採集與 AI 分析

以 M5Stack 內建陀螺儀記錄排球揮臂峰值角速度，並透過 Flask 網頁呈現訓練統計、K-Means 強度分群及線性疲勞趨勢。

## 專案內容

- `volleyball_m5stack.ino`：M5StickC Plus 韌體，讀取 IMU、將揮臂資料存入 Flash，並提供手機 Wi-Fi 熱點與儀表板。
- `app.py`：Flask 網站、資料 API、統計與機器學習分析。
- `templates/index.html`：Bootstrap 儀表板、Chart.js 圖表、訓練記錄與 CSV 匯出。
- `requirements.txt`：Python 依賴套件。

## 環境需求

- Python 3.10 或更新版本。
- Arduino IDE 2.x，已安裝 ESP32 開發板支援。
- M5Stack 裝置及 USB 資料線。
- USB 資料線連接 M5Stack 與電腦；不需要 Wi-Fi。

## 本機啟動

在專案資料夾開啟 PowerShell 或終端機：

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python app.py
```

先在 Windows「裝置管理員 → 連接埠 (COM 和 LPT)」確認 M5Stack 的 COM 埠，例如 `COM5`。關閉 Arduino IDE 的序列監控視窗，然後在 PowerShell 啟動網站（把 `COM5` 換成實際埠號）：

```powershell
$env:SERIAL_PORT = "COM5"
.\.venv\Scripts\python.exe app.py
```

瀏覽器開啟 <http://127.0.0.1:5000>。電腦可繼續使用校園有線網路；M5Stack 透過 USB 序列埠將資料送給 Flask。若 PowerShell 阻擋虛擬環境啟用，可在目前終端執行 `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass` 後再啟用。

也可以使用一行安裝依賴：

```powershell
pip install Flask scikit-learn pyserial
```

## M5StickC Plus 無線使用

韌體會建立自己的 Wi-Fi 熱點，不需要電腦連 Wi-Fi、學校網路或網頁登入：

- Wi-Fi 名稱：`SpikeLab-M5`
- Wi-Fi 密碼：`SpikeLab2026`
- 手機連上後瀏覽：<http://192.168.4.1>

手機連這個熱點時通常無法同時使用行動網路；網頁與資料由 M5StickC Plus 本機提供。揮臂資料會先存入裝置 Flash，M5StickC Plus 可拔離電腦後繼續記錄。手機頁面顯示即時次數、平均、CV%、角速度趨勢與近期紀錄；電腦版的完整 K-Means 分群和線性疲勞分析仍需接回電腦處理。

若要匯入電腦版 Flask 分析：

1. 先在手機頁面按「匯出 CSV」保存副本。
2. 用 USB 把 M5StickC Plus 接回電腦，關閉 Arduino IDE 序列監控視窗。
3. 在 PowerShell 設定實際 COM 埠並啟動 Flask，例如 `$env:SERIAL_PORT = "COM3"`，再執行 `.\.venv\Scripts\python.exe app.py`。
4. 後端會自動向 M5StickC Plus 同步 Flash 中未匯入的記錄，確認收到後才清除裝置端待同步檔案。

## M5Stack 燒錄

1. Arduino IDE 的開發板管理員安裝 ESP32 支援，並從 Library Manager 安裝 **M5Unified** 與 **ArduinoJson**。
2. 使用 USB 資料線連接 M5Stack，開啟 `volleyball_m5stack.ino`，選取對應的 ESP32 開發板與序列埠，編譯並上傳。韌體不需要填學校 Wi-Fi 名稱、密碼或電腦 IP。
3. 燒錄本專案設定的目標為 M5StickC Plus。若換用其他 M5Stack 型號，需先調整板卡目標及螢幕版面。

預設偵測門檻為 `350 deg/s`，並以靜止時的陀螺儀噪聲估計動態門檻。偵測到揮臂後，裝置等角速度低於門檻的 55% 持續 100 ms 才結束事件；兩次事件至少間隔 800 ms。可依實際配戴位置與動作調整韌體常數。

M5Stack 透過 USB 傳送一行 JSON。`timestamp` 是裝置開機後的毫秒數，不是校正後的日曆時間；`peak_dps` 是合成角速度峰值；`duration_ms` 是揮臂事件歷時。Arduino 序列監控視窗與 Flask 不能同時占用同一個 COM 埠。

## 統計與 AI 指標

- 平均值：所有揮臂峰值角速度的算術平均。
- 標準差：樣本標準差；只有一筆資料時回傳 0。
- CV%：樣本標準差除以平均值再乘 100%，用於觀察動作一致性，數值越低代表波動越小。
- K-Means：資料至少有 3 筆且有 3 種以上不同峰值時進行分群，依群中心由低至高命名為輕擊、標準扣球、強力重扣。群數不足或資料不足時會顯示尚無法分類。
- 疲勞趨勢：以揮臂序號預測峰值角速度的線性回歸斜率，負斜率只代表本次記錄呈下降趨勢，不代表醫療或體能診斷。

所有資料目前存於 Flask 程序記憶體中，停止或重啟伺服器會清空。儀表板「重設訓練」會清除後端所有記錄；M5Stack BtnA 只重設裝置螢幕上的本地計數，不會刪除後端資料。

## API

- `POST /api/swing`：接收 `{"timestamp": 1234, "peak_dps": 480.5, "duration_ms": 620}`。
- `GET /api/report`：回傳揮臂列表、Mean、StdDev、CV%、分群和疲勞趨勢。
- `POST /api/reset`：清除本次伺服器記憶體中的訓練記錄。
- `GET /api/export.csv`：下載 CSV 訓練記錄。

## 快速測試 API

伺服器啟動後，可用 PowerShell 送一筆測試資料：

```powershell
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:5000/api/swing -ContentType 'application/json' -Body '{"timestamp":1234,"peak_dps":480.5,"duration_ms":620}'
Invoke-RestMethod -Uri http://127.0.0.1:5000/api/report
```