# UTM klavye düzeltmesi

İncelenen eski makinede `QEMU.PS2Controller = false` ve USB 3.0 giriş
etkindi. SHENLONG v0.2 yalnızca PS/2 klavye sürücüsü içerir; USB HID/xHCI
sürücüsü yoktur. ISO'yu değiştirmek UTM'nin bu ayarını değiştirmez.

Hazır `SHENLONG-v0.2-input-fixed.utm` paketini UTM ile açın ve
**SHENLONG v0.2 - Input Fixed** makinesini başlatın. ISO paketin içindedir.
Terminal ekranına tıklayın, `help` yazıp Enter'a basın.

Eski makineyi kullanmak için önce tamamen kapatın (pause/suspend değil):

1. Edit → QEMU → **Force PS/2 controller** seçeneğini açın.
2. Edit → Input → **USB Support: Disabled** seçin.
3. Kaydedip soğuk başlatın. Askıya alınmış eski oturuma dönmeyin.

Resmî açıklama: https://docs.getutm.app/settings-qemu/qemu/#force-ps2-controller

## Doğrulama

UTM'nin kurulu QEMU 10.0.12 motoru, Q35, x86_64 emülasyon, 256 MiB,
virtio-vga ve UEFI ile ISO gerçekten başlatıldı. Test gerçek tuş basma/bırakma
olaylarını gönderir ve framebuffer'daki metni okuyarak sonuçları denetler.
USB-only yapılandırmada hata yeniden üretildi. PS/2 yapılandırmada yazma,
Enter, Backspace, Shift, Caps Lock, Shift+Caps ve komut çalıştırma doğrulandı.
UTM GUI otomasyonu macOS tarafından reddedildi (-1743); testler ayrı,
headless QEMU oturumunda yürütüldü. Kullanıcının mevcut VM diski değiştirilmedi.

```sh
clang tools/utm-qemu-runner.c \
  -Wl,-rpath,/Applications/UTM.app/Contents/Frameworks \
  -o build/out/utm-qemu-runner
python3 tools/test-input.py
python3 tools/test-input.py --usb-only
python3 tools/package-utm.py build/SHENLONG-v0.2-input-fixed.utm
```

Paketleyici var olan bir `.utm` dosyasının üzerine yazmaz.
