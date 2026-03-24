# Import the Windows API to read hardware key states
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class MouseHook {
    [DllImport("user32.dll")]
    public static extern short GetAsyncKeyState(int vKey);
}
"@

$VK_LBUTTON = 0x01
$VK_MBUTTON = 0x04

Clear-Host
Write-Host "Real-Time Mouse Logger Active." -ForegroundColor Yellow
Write-Host "Press your physical Middle Mouse Button to start the firmware test."
Write-Host "Press Ctrl+C in this window to exit completely.`n"

$isRunning = $false
$clickCount = 0
$lastClickTime = $null

# State tracking to prevent thousands of logs from a single button hold
$lbtnPressed = $false
$mbtnPressed = $false

try {
    while ($true) {
        # Check the hardware state of the buttons (0x8000 means it is currently down)
        $lbtnState = [MouseHook]::GetAsyncKeyState($VK_LBUTTON) -band 0x8000
        $mbtnState = [MouseHook]::GetAsyncKeyState($VK_MBUTTON) -band 0x8000

        # --- Middle Button Logic (Start/Stop) ---
        if ($mbtnState -and -not $mbtnPressed) {
            $mbtnPressed = $true
            if (-not $isRunning) {
                $isRunning = $true
                $clickCount = 0
                $lastClickTime = [datetime]::Now
                Write-Host ("[{0:HH:mm:ss.fff}] Firmware Sequence Started!" -f [datetime]::Now) -ForegroundColor Green
            } else {
                $isRunning = $false
                Write-Host ("[{0:HH:mm:ss.fff}] Firmware Sequence Stopped! Total Clicks: {1}`n" -f [datetime]::Now, $clickCount) -ForegroundColor Red
            }
        } elseif (-not $mbtnState) {
            $mbtnPressed = $false
        }

        # --- Left Button Logic (Interval Tracking) ---
        if ($lbtnState -and -not $lbtnPressed) {
            $lbtnPressed = $true
            if ($isRunning) {
                $clickCount++
                $now = [datetime]::Now
                $interval = ($now - $lastClickTime).TotalMilliseconds
                $lastClickTime = $now
                Write-Host ("[{0:HH:mm:ss.fff}] Left Click | Count: {1}/120 | Interval: {2:N0} ms" -f $now, $clickCount, $interval) -ForegroundColor Cyan
            }
        } elseif (-not $lbtnState) {
            $lbtnPressed = $false
        }

        # Sleep for 1 millisecond to prevent the while-loop from maxing out your CPU core
        Start-Sleep -Milliseconds 1
    }
}
catch {
    Write-Host "`nExiting logger..."
}