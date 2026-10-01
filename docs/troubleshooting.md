# Troubleshooting

This document shows common issues and possible solutions while using the device features.

- [Troubleshooting](#troubleshooting)
    - [Cannot See the Device on the Network](#cannot-see-the-device-on-the-network)
    - [Connection Drops or Times Out](#connection-drops-or-times-out)
    - [Upload Fails](#upload-fails)
    - [Saved Password Not Working](#saved-password-not-working)

### Cannot See the Device on the Network

**Problem:** Browser shows "Cannot connect" or "Site can't be reached"

**Solutions:**

1. Verify both devices are on the correct network
   - Check your computer/phone Wi-Fi settings
   - In **Join Network** mode, your computer/phone and CrossPoint Reader must be on the same Wi-Fi network
   - In **Create Hotspot** mode, your computer/phone must be connected to the `CrossPoint-Reader` hotspot
2. Double-check the IP address
   - Make sure you typed it correctly
   - Include `http://` at the beginning
   - Try the displayed IP address if `http://crosspoint.local/` does not resolve
3. Try disabling VPN if you're using one
4. Some networks have "client isolation" enabled - use Create Hotspot mode or check with your network administrator

### Connection Drops or Times Out

**Problem:** Wi-Fi connection is unstable

**Solutions:**

1. Move closer to the Wi-Fi router, or use Create Hotspot mode for a direct connection
2. Check signal strength on the device (should be at least `||` or better)
3. Avoid interference from other devices
4. Try a different Wi-Fi network if available

### Upload Fails

**Problem:** File upload doesn't complete or shows an error

**Solutions:**

1. Check that the SD card has enough free space
2. Check that the filename is valid for the SD card filesystem
3. Try uploading a smaller file first to test
4. Refresh the browser page and try again
5. If WebSocket upload fails repeatedly, refresh the page and retry with the HTTP fallback path

### Saved Password Not Working

**Problem:** Device fails to connect with saved credentials

**Solutions:**

1. When connection fails, you'll be prompted to "Forget Network"
2. Select **Yes** to remove the saved password
3. Reconnect and enter the password again
4. Choose to save the new password

### Smart (WPM) Auto Page Turn Flipping Too Fast or Too Slow

**Problem:** In Smart mode, pages turn faster or slower than you read.

**Solutions:**

1. Open the **Reader Menu** (press **Confirm** while reading) and select **Reset Reading Speed** to clear the learned WPM and start fresh.
2. The status bar shows the current learned speed (e.g. `Auto Turn Enabled: 230 wpm`) or `Auto Turn Enabled: Uncalibrated` if no speed has been learned yet. Use this to confirm the mode is active.
3. Speed is learned from manual page turns: pressing **Next Page** before the timer fires nudges the speed up; pressing **Previous Page** nudges it down. Turning pages consistently will calibrate the speed over several pages.
4. Very short page turns (under 2 seconds) are ignored as accidental taps and do not affect the learned speed.
5. If you walked away mid-page, the stored WPM may have drifted lower than your actual reading speed — use **Reset Reading Speed** to recover.

### Auto Page Turn Timer Does Not Advance

**Problem:** In Smart mode the page does not flip automatically.

**Solutions:**

1. Ensure **Auto Turn (Pages Per Minute)** is set to **Smart (WPM)** and not **Off** — check via **Reader Menu → Auto Turn (Pages Per Minute)**.
2. If you pressed **Previous Page** one or more times, the auto-turn timer is paused until you manually press **Next Page** enough times to return to where you left off (one forward press required per backward press). This prevents an unwanted auto-flip while you are browsing back through the book.
3. Selecting **Reset Reading Speed** from the Reader Menu will also un-pause the timer immediately.
4. If the status bar shows `Auto Turn Paused`, a short power button press paused it. Press the power button again to resume.
5. As a last resort, open the **Reader Menu**, set **Auto Turn (Pages Per Minute)** to **Off**, then set it back to **Smart (WPM)** — this resets the counter and restarts the timer.
