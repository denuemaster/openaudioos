# OpenAudioOS M0.1 Patch

Copy these files into your repository root.

```bash
cp -R OpenAudioOS-M0.1-patch/* ~/Developer/openaudioos/
cd ~/Developer/openaudioos

rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

First boot:
- Connect to WiFi `OpenAudioOS-Setup`
- Password: `openaudio`
- Open `http://192.168.4.1`
- Save WiFi credentials
