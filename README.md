# Spike Lab：M5StickC Plus 排球揮臂分析

Spike Lab 使用 M5StickC Plus 內建陀螺儀記錄揮臂角速度。裝置會自行建立 Wi-Fi 熱點，將訓練資料保存在內建 Flash，手機連線後即可查看即時統計。也可以稍後接回電腦，使用 Flask 網站進行 K-Means 強度分群與疲勞趨勢分析。

## 功能

- 偵測揮臂事件，記錄峰值角速度與事件歷時。
- M5StickC Plus 自建 Wi-Fi 熱點，不需要加入學校 Wi-Fi 或使用網頁登入。
- 手機儀表板顯示揮臂次數、平均角速度、CV%、趨勢圖與近期記錄。
- 訓練資料保存在裝置 Flash；關機再開機後仍會保留。
- 手機頁面可匯出 CSV；接回電腦後可用完整 Flask 網站查看 AI 分析。

## 支援硬體

目前韌體與螢幕版面以 **M5StickC Plus** 為目標。其他 M5Stack 型號的板卡設定、按鈕或螢幕尺寸可能不同，尚未保證可直接使用。

## 取得專案

安裝 Git 後，在終端機執行：

```sh
git clone https://github.com/apple15165465/volleyballproject.git
```

也可以在 GitHub repository 頁面選 **Code → Download ZIP**，解壓縮後使用。

## 安裝到 M5StickC Plus

### 1. 安裝 Arduino 工具

安裝 Arduino IDE 2.x，並完成以下設定：

- Boards Manager：安裝 ESP32 開發板支援。
- Library Manager：安裝 `M5Unified` 和 `ArduinoJson`。

### 2. 設定自己的熱點憑證

在 Arduino IDE 開啟 `volleyball_m5stack.ino`，找到以下設定並改成自己的名稱和密碼：

```cpp
constexpr char AP_SSID[] = "SpikeLab-M5";
constexpr char AP_PASSWORD[] = "SpikeLab2026";
```

這兩項會編譯進裝置韌體。範例密碼公開在 GitHub 上，**請務必在燒錄前更換**，並設定至少 8 個字元的密碼。不要把自己的私人密碼提交到 GitHub。

### 3. 編譯並上傳

1. 用 USB 資料線連接 M5StickC Plus 和電腦。
2. Arduino IDE 的開發板選擇 **M5StickC Plus**，再選擇對應的 USB 序列埠。
3. 按「上傳」並等待完成。若無法上傳，確認資料線支援資料傳輸，並關閉其他占用序列埠的程式。
4. 上傳完成後，裝置會自動啟動熱點；之後可拔掉 USB，使用電池操作。

## 用手機查看訓練

1. 在手機 Wi-Fi 設定中連上剛才設定的熱點名稱。
2. 輸入你設定的熱點密碼。若手機提示「沒有網際網路」，選擇仍要連線或保持連線。
3. 用手機瀏覽器開啟：<http://192.168.4.1>
4. 開始揮臂。數據會自動更新並保存在 M5StickC Plus 的 Flash。

手機連上 M5StickC Plus 熱點期間，可能無法同時使用一般 Wi-Fi 或行動網路。結束訓練後，可在儀表板下載 CSV 保存資料。

## 在電腦使用完整 AI 分析

電腦版需要 Python 3.10 或更新版本。它不需要電腦連 Wi-Fi，但同步時需要用 USB 連接 M5StickC Plus。

在專案資料夾開啟終端機：

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

確認裝置的序列埠：Windows 可在「裝置管理員 → 連接埠 (COM 和 LPT)」查看，例如 `COM5`。關閉 Arduino IDE 的序列監控視窗，然後在 PowerShell 設定序列埠並啟動 Flask：

```powershell
$env:SERIAL_PORT = "COM5"
python app.py
```

將 `COM5` 換成你的實際埠號。Flask 啟動時會自動向裝置索取尚未同步的記錄；確認收到後，才清除裝置端的待同步資料。電腦瀏覽器開啟 <http://127.0.0.1:5000>，即可查看平均值、樣本標準差、CV%、K-Means 強度分群、疲勞趨勢和 CSV 匯出。

如果 PowerShell 不允許啟用虛擬環境，可在目前終端機執行：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

再重新執行啟用指令。

## 揮臂偵測與統計

- 基礎揮臂門檻：`350 deg/s`；韌體會依靜止時的陀螺儀雜訊調整動態門檻。
- 揮臂結束條件：角速度低於門檻的 55% 並持續 100 ms。
- 防抖間隔：兩次揮臂至少相隔 800 ms。
- CV%：樣本標準差除以平均值再乘 100%，可用來觀察揮臂強度的一致性。
- K-Means：有足夠資料時依峰值角速度分成輕擊、標準扣球和強力重扣。
- 線性回歸：依揮臂次序估計角速度變化趨勢；結果僅供訓練參考，不是醫療或體能診斷。

手機端資料保存在 M5StickC Plus Flash。電腦端 Flask 的統計資料目前保存在伺服器程序記憶體，停止或重新啟動 Flask 後會清空；需要長期保存時，請匯出 CSV。

## 安全提醒

- 每台裝置使用不同的 Wi-Fi 熱點密碼，並在公開程式中使用非個人專用的範例值。
- 若曾把私人密碼提交到公開 repository，請立即更換密碼並重新燒錄韌體；只刪除程式碼不會讓舊密碼失效。
- `.env`、虛擬環境和本機建置產物已列入 `.gitignore`，不應上傳到 GitHub。
