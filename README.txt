HA 8KEY - ZMK CONFIG

Mục tiêu:
- nice!nano-compatible nRF52840 / ProMicro footprint
- 8 nút direct GPIO, mỗi nút nối GPIO -> GND
- USB HID + BLE HID
- ZMK Studio qua USB

Build:
1. Tạo repo GitHub trống.
2. Upload toàn bộ NỘI DUNG của thư mục HA_8KEY_ZMK vào root repo.
3. GitHub Actions sẽ chạy workflow Build.
4. Khi build xanh, tải artifact firmware.
5. Double-reset board để hiện ổ NICENANO.
6. Copy file .uf2 của ha8key vào ổ NICENANO.

LƯU Ý:
- Chưa hàn theo số chân cho tới khi đối chiếu mapping ProMicro của đúng clone board.
- Không nối VCC/VDD vào nút. Nút chỉ nối GPIO với GND.
