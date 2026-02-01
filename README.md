# XIAONunchuk

Wii ヌンチャク RVL-004をBluetoothマウスとして使う。  
PlatformIOでSeeed Studio XIAO ESP32C3を利用する前提のプログラムになっている。  
バッテリーで利用したかったので、ATOMS3Nunchukのリポジトリにバッテリー管理のコードを足した感じになった。

I2Cのヌンチャクとのやり取りは[DigistumpNunchuk](https://github.com/bigw00d/DigistumpNunchuk)のコードを利用。  
Bluetoothマウスの処理は[ESP32-BLE-Mouse](https://github.com/T-vK/ESP32-BLE-Mouse)を利用。

## License notice

以下のリポジトリのソースコードを一部利用している。  
[bigw00d/DigistumpNunchuk](https://github.com/bigw00d/DigistumpNunchuk) Apache License, Version 2.0