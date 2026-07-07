# OpenAudioOS M0.15 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.15-patch.zip
cp -R OpenAudioOS-M0.15-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- iPhone should still see OpenAudioOS
- Selecting it should not reboot the ESP32
- Log should show OPTIONS and POST /fp-setup
- `/fp-setup` should be logged with body length and first bytes
- OpenAudioOS should return a clean 501 for fp-setup
