# SHENLONG yerel ağ, Web ve medya

Web ve Medya uygulamaları SHENLONG çekirdeğinde çalışır. Linux, host
tarayıcısı, Selenium, görüntü köprüsü veya HTTP proxy kullanılmaz.
UTM yalnızca RTL8139 ağ kartını ve AC97 ses donanımını emüle eder.

## Kullanım

Yeni `SHENLONG-v0.7-light.utm` paketini soğuk başlatın. Host Wi-Fi ağına
macOS'tan bağlanın; Ethernet de kullanılabilir. Ağ penceresindeki
**Bağlan** sanal bağlantıyı etkinleştirir. Wi-Fi parolası konuk sisteme
girilmez. **Host ağı hazır**, ağ geçidinin erişilebilir olduğunu gösterir;
her sitenin açılacağı anlamına gelmez.

Web'de `https://www.google.com/` veya `https://example.com/` yazın.
Ctrl+L adresi seçer, Enter açar, Esc indirmeyi durdurur.
Bağlantılar tıklanabilir; yön/Page Up/Down tuşları metni kaydırır.
Terminalde `net [on|off]`, `web https://adres/` ve `web status` vardır.

## HTTPS

BearSSL 0.6 ile yerel TLS 1.2, SNI, ECDHE ve AES-GCM/ChaCha20-Poly1305
kullanılır. Sertifika zinciri, alan adı ve geçerlilik tarihi doğrulanır.
118 Mozilla kökü 5 Kasım 2026 tarihli curl CA paketinden gömülüdür.
Tarih firmware/RTC zamanından gelir. Rastgelelik için RDRAND zorunludur;
UTM paketindeki CPU `max` bu özelliği sunar. Güvenli başlangıç koşulları
yoksa bağlantı reddedilir; doğrulamayı kapatma düğmesi yoktur.
HTTPS → HTTP otomatik yönlendirmesi engellenir.

Bazı sunucular TLS kapanış uyarısını göndermeden TCP'yi kapatır. Bu durumda
yalnızca doğrulanmış TLS kayıtlarında tam Content-Length veya son chunk
alınmışsa yanıt kabul edilir; sınırı yalnızca kapanışla belirlenen veya
eksik yanıt reddedilir. Bu davranış
[RFC 9112 §9.8](https://www.rfc-editor.org/rfc/rfc9112.html#section-9.8)
ile uyumludur. Sertifika ve kayıt doğrulaması atlanmaz.

TLS 1.3, OCSP/CRL iptal denetimi, istemci sertifikası ve HTTP/2 yoktur.
Bu deneysel istemci genel amaçlı, güvenlik denetiminden geçmiş bir
tarayıcı değildir; kök paketinin zaman içinde güncellenmesi gerekir.

## HTML ve medya kapsamı

HTTP/1.0–1.1, Content-Length, chunked yanıt, göreli bağlantılar ve en çok
beş yönlendirme desteklenir. Yanıt en fazla 2 MiB, metin 384 satır,
bağlantı sayısı 48'dir. HTML metni, başlık ve bağlantılar gösterilir;
ISO-8859-1 / ISO-8859-9 metinleri UTF-8'e çevrilir. Sıkıştırılmış
yanıt kabul edilmez; sunucudan identity kodlaması istenir.

**Tam web/YouTube oynatımı henüz yoktur.** JavaScript, DOM/CSS yerleşimi,
form gönderimi, çerezli oturumlar, modern video kodekleri ve DRM bulunmaz.
Google'ın HTML metnine HTTPS ile erişim, tam etkileşimli Google/YouTube
desteği anlamına gelmez. Hostta bir tarayıcı çalıştırılmaz.

Medya, PL_MPEG ile **MPEG-1 video + MP2 stereo ses** çözer. En çok
1280×720 ve 2 MiB dosya desteklenir; bütün dosya indirilmeden oynatılmaz.
`video/mpeg` Content-Type döndüren doğrudan HTTP/HTTPS bağlantısı Medya'yı
açar. H.264, VP9, AV1, AAC, Opus, HLS/DASH ve MP4/WebM desteklenmez.
Akış zaman damgalarıyla gelişmiş senkronizasyon ve arama/seek yoktur.

Dock'tan Medya → **Örneği oynat**, veya terminalden `video demo`,
gömülü 4 saniyelik 640×360 / 25 fps test videosunu 440 Hz stereo tonla
oynatır. `video stop` durdurur; `video status` çözülen kareleri ve ses
aygıtının tükettiği PCM karelerini gösterir. Pencere kapanınca ses durur;
küçültülünce oynatma sürer. Ses düğmesi susturur/açar.

AC97 PCI sürücüsü 32 tamponlu DMA kuyruğu kullanır; 32/44,1/48 kHz stereo
PCM kabul eder. Medya çözücü belleği 16 MiB ile sınırlıdır. Konuk içinde
çalışan çözücü kullanılır; hostta video dönüştürülmez. Test videosunun
üretimi ve üçüncü taraf kaynaklar [THIRD-PARTY.md](THIRD-PARTY.md) içindedir.

## Sanal donanım

UTM: x86_64 / Q35, CPU max, en az 256 MiB RAM, Emulated NAT / RTL8139,
AC97 ses, virtio-vga, PS/2 açık. Gelen port yönlendirmesi yoktur.
IPv4 sabit `10.0.2.15/24`, ağ geçidi `10.0.2.2`, DNS `10.0.2.3`.
DHCP, IPv6 ve fiziksel Wi-Fi sürücüsü bulunmaz.

QEMU ağ/ses ayarları (macOS):

```sh
-cpu max -netdev user,id=hostnet \
-device rtl8139,netdev=hostnet,mac=52:54:00:53:48:05 \
-audiodev coreaudio,id=audio -device AC97,audiodev=audio
```

Ağ sürücüsü PCI, sınırlı DMA tamponları ve polling kullanır. ARP, IPv4,
DNS A ve tek TCP bağlantısı vardır. Tekrar gönderim, sıra/ACK denetimi,
30 saniye zaman aşımı uygulanır. IP parçalama desteklenmez.

## Doğrulama

`make test-network`, yerel HTTP test sunucusuna gerçek emüle NIC üzerinden
erişir: bağlantılar, chunked yanıt, yönlendirme, büyük yanıt, kaydırma,
saatin ilerlemesi ve bağlantı aç/kapat sınanır. Test sunucusu çalışma
zamanı bağımlılığı değildir.

`make test-native` genel internet gerektirir. Ağdan MPEG indirir; gerçek
framebuffer karelerinin değiştiğini, 100 kare video çözümünü, 177.408 PCM
karenin AC97 tarafından tüketildiğini denetler. example.com ve Google
HTTPS yanıtları ile süresi dolmuş, alan adı uyuşmayan ve kendinden
imzalı badssl sertifikalarının reddini sınar. Sonuçlar
`build/out/native-results.json`, gerçek emüle ses çıkışı
`build/out/native-audio.wav` içindedir.

`make test` ayrıca URL/HTTP/karakter kodlaması, video çözücüsü,
kesik/bozuk video verileri, klavye, saat, compositor ve UEFI/BIOS UI
testlerini çalıştırır. Ayrıştırıcı/çözücü testleri ASan ve UBSan kullanır.

## Kaynaklar

- [BearSSL TLS motoru](https://bearssl.org/api1.html)
- [Mozilla kök paketi / curl](https://curl.se/docs/caextract.html)
- [PL_MPEG](https://github.com/phoboslab/pl_mpeg)
- [QEMU AC97](https://github.com/qemu/qemu/blob/master/hw/audio/ac97.c)
- [QEMU RTL8139](https://github.com/qemu/qemu/blob/master/hw/net/rtl8139.c)
- [UTM ses yapılandırması](https://github.com/utmapp/UTM/blob/main/Configuration/UTMQemuConfigurationSound.swift)
