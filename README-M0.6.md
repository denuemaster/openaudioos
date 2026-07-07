# OpenAudioOS M0.6 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.6-patch.zip
cp -R OpenAudioOS-M0.6-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock
export IDF_COMPONENT_MANAGER=0

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- WebUI still loads
- Tone still plays
- Start/stop works
- Volume/frequency works
- `/api/status` now shows ringbuffer stats
